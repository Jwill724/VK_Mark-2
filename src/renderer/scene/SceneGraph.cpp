#include "pch.h"

#include "SceneGraph.h"
#include "utils/BufferUtils.h"
#include "renderer/Renderer.h"
#include "core/AssetManager.h"
#include "renderer/scene/Visibility.h"
#include "engine/JobSystem.h"
#include "RenderScene.h"

void SceneGraph::buildSceneGraph(
	ThreadContext& threadCtx,
	std::vector<GlobalInstance>& globalInstances,
	std::vector<glm::mat4>& globalTransforms)
{
	ASSERT(threadCtx.workQueueActive != nullptr);
	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	ASSERT(queue);

	auto gltfJobs = queue->collect();

	uint32_t instanceCounter = 0;
	uint32_t firstTransform = 0;

	for (auto& context : gltfJobs) {
		if (!context->isComplete()) continue;

		auto& gltf = context->gltfAsset;
		auto& modelAsset = *context->scene;

		// === Build all nodes ===
		std::vector<std::shared_ptr<Node>> nodes;
		nodes.reserve(gltf.nodes.size());
		for (size_t i = 0; i < gltf.nodes.size(); ++i) {
			const auto& srcNode = gltf.nodes[i];
			auto node = std::make_shared<Node>();

			std::visit(fastgltf::visitor{
				[&](const fastgltf::math::fmat4x4& matrix) {
					node->localTransform = glm::make_mat4x4(matrix.data());
				},
				[&](const fastgltf::TRS& transform) {
					glm::vec3 tl(transform.translation[0], transform.translation[1], transform.translation[2]);
					glm::quat rot(transform.rotation[3], transform.rotation[0], transform.rotation[1], transform.rotation[2]);
					glm::vec3 sc(transform.scale[0], transform.scale[1], transform.scale[2]);
					node->localTransform =
						glm::translate(glm::mat4(1.0f), tl) *
						glm::toMat4(rot) *
						glm::scale(glm::mat4(1.0f), sc);
				}
			}, srcNode.transform);

			nodes.push_back(node);
		}

		// === Parent-child relationships ===
		for (size_t i = 0; i < gltf.nodes.size(); ++i) {
			for (auto childIdx : gltf.nodes[i].children) {
				nodes[i]->children.push_back(nodes[static_cast<size_t>(childIdx)]);
				nodes[static_cast<size_t>(childIdx)]->parent = nodes[i];
			}
		}

		// === Find root nodes ===
		modelAsset.sceneNodes.topNodes.clear();
		for (auto& node : nodes) {
			if (node->parent.expired()) {
				modelAsset.sceneNodes.topNodes.push_back(node);
			}
		}

		// === Compute world transforms ===
		for (auto& node : modelAsset.sceneNodes.topNodes) {
			node->refreshTransform(glm::mat4(1.0f));
		}
		modelAsset.sceneNodes.nodes = nodes;

		// define model with a sceneID
		SceneID sceneID = SceneIDs.at(modelAsset.sceneName);
		modelAsset.sceneID = sceneID;

		// === Assign global instances ===
		GlobalInstance gblInst{};
		gblInst.sceneID = static_cast<uint8_t>(sceneID);
		gblInst.instanceID = instanceCounter++;

		const auto& bakedInstances = modelAsset.runtime.bakedInstances;
		const auto& bakedNodeIDs = modelAsset.runtime.bakedNodeIDs;

		ASSERT(bakedNodeIDs.size() == bakedInstances.size() && "[BuildSceneGraph]: bakedNodes should equal bakedInstances.");

		// Build unique node set + local->slot map from bakedNodeIDs
		modelAsset.runtime.uniqueNodeIDs.clear();
		modelAsset.runtime.localToNodeSlot.resize(bakedNodeIDs.size());

		std::unordered_map<uint32_t, uint32_t> nodeToSlot;
		modelAsset.runtime.uniqueNodeIDs.reserve(bakedNodeIDs.size());

		for (size_t i = 0; i < bakedNodeIDs.size(); ++i) {
			const uint32_t nodeIdx = static_cast<uint32_t>(bakedNodeIDs[i]);
			auto it = nodeToSlot.find(nodeIdx);
			uint32_t slot = 0;
			if (it == nodeToSlot.end()) {
				slot = static_cast<uint32_t>(modelAsset.runtime.uniqueNodeIDs.size());
				nodeToSlot.emplace(nodeIdx, slot);
				modelAsset.runtime.uniqueNodeIDs.push_back(nodeIdx);
			}
			else {
				slot = it->second;
			}
			modelAsset.runtime.localToNodeSlot[i] = slot;
		}

		gblInst.perInstanceStride = static_cast<uint32_t>(bakedInstances.size());
		gblInst.transformCount = static_cast<uint32_t>(modelAsset.runtime.uniqueNodeIDs.size());

		// === Push unique transforms into the global list ===
		gblInst.firstTransform = firstTransform;
		for (uint32_t i = 0; i < gblInst.transformCount; ++i) {
			const uint32_t nodeIdx = modelAsset.runtime.uniqueNodeIDs[i];
			globalTransforms.push_back(nodes[nodeIdx]->worldTransform);
		}
		firstTransform += gblInst.transformCount;

		globalInstances.push_back(gblInst);

		JobSystem::log(threadCtx.threadID,
			fmt::format("SceneGraph built: '{}'. Total bakedInstances = {}. Total materials = {}. Total transforms = {}\n",
				modelAsset.sceneName,
				bakedInstances.size(),
				modelAsset.runtime.materials.size(),
				gblInst.transformCount));

		queue->push(context);
	}
}