#include "pch.h"

#include "SceneGraph.h"
#include "common/ResourceTypes.h"
#include "core/ResourceManager.h"
#include "utils/BufferUtils.h"
#include "utils/RendererUtils.h"
#include "vulkan/Backend.h"
#include "core/AssetManager.h"
#include "renderer/RenderScene.h"
#include "renderer/Visibility.h"

void SceneGraph::buildSceneGraph(
	ThreadContext& threadCtx,
	std::vector<GPUMeshData>& meshes,
	std::vector<glm::mat4>& bakedTransformsList)
{
	ASSERT(threadCtx.workQueueActive != nullptr);
	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	ASSERT(queue);

	auto gltfJobs = queue->collect();
	for (auto& context : gltfJobs) {
		if (!context->isComplete()) continue;

		auto& gltf = context->gltfAsset;
		auto& modelAsset = *context->scene;

		fmt::print("Processing GLTF scene: {}\n", modelAsset.sceneName);
		fmt::print("GLTF has {} nodes\n", gltf.nodes.size());

		std::vector<std::shared_ptr<Node>> nodes;
		nodes.reserve(gltf.nodes.size());

		// === Build all nodes ===
		for (size_t i = 0; i < gltf.nodes.size(); ++i) {
			const auto& srcNode = gltf.nodes[i];
			auto node = std::make_shared<Node>();
			node->nodeID = static_cast<uint32_t>(i);

			std::visit(fastgltf::visitor{
				[&](const fastgltf::math::fmat4x4& matrix) {
					memcpy(&node->localTransform, matrix.data(), sizeof(matrix));
				},
				[&](const fastgltf::TRS& transform) {
					glm::vec3 tl(transform.translation[0], transform.translation[1], transform.translation[2]);
					glm::quat rot(transform.rotation[3], transform.rotation[0], transform.rotation[1], transform.rotation[2]);
					glm::vec3 sc(transform.scale[0], transform.scale[1], transform.scale[2]);
					node->localTransform = glm::translate(glm::mat4(1.0f), tl) *
						glm::toMat4(rot) * glm::scale(glm::mat4(1.0f), sc);
				}
			}, srcNode.transform);

			nodes.push_back(node);
		}

		// === Parent-child relationships ===
		for (size_t i = 0; i < gltf.nodes.size(); ++i) {
			for (auto childIdx : gltf.nodes[i].children) {
				nodes[i]->children.push_back(nodes[childIdx]);
				nodes[childIdx]->parent = nodes[i];
				fmt::print("  Parent {} -> Child {}\n", static_cast<uint32_t>(i), childIdx);
			}
		}

		// === Find root nodes ===
		modelAsset.scene.topNodes.clear();
		for (auto& node : nodes) {
			if (node->parent.expired()) {
				modelAsset.scene.topNodes.push_back(node);
			}
		}
		fmt::print("Found {} root nodes\n", modelAsset.scene.topNodes.size());

		// === Compute world transforms ===
		for (auto& node : modelAsset.scene.topNodes) {
			node->refreshTransform(glm::mat4(1.0f));
		}

		modelAsset.scene.nodes = nodes;

		// === Assign transformIDs and store baked world transforms ===
		modelAsset.scene.nodeIDToTransformID.resize(modelAsset.scene.nodes.size(), UINT32_MAX);
		bakedTransformsList.reserve(bakedTransformsList.size() + modelAsset.scene.nodes.size());

		for (uint32_t i = 0; i < modelAsset.scene.nodes.size(); ++i) {
			const glm::mat4& world = modelAsset.scene.nodes[i]->worldTransform;
			modelAsset.scene.nodeIDToTransformID[i] = static_cast<uint32_t>(bakedTransformsList.size());
			bakedTransformsList.push_back(world);
		}

		// === Assign baked instances directly to nodes ===
		uint32_t instanceCounter = 0;

		for (uint32_t nodeIndex = 0; nodeIndex < modelAsset.scene.nodes.size(); ++nodeIndex) {
			auto& node = modelAsset.scene.nodes[nodeIndex];
			uint32_t transformID = modelAsset.scene.nodeIDToTransformID[nodeIndex];

			auto it = modelAsset.runtime.nodeIndexToBakedInstances.find(nodeIndex);

			if (it == modelAsset.runtime.nodeIndexToBakedInstances.end()) continue;

			auto& bakedList = it->second;
			for (auto& baked : bakedList) {
				if (!baked) continue;

				baked->nodeID = nodeIndex;
				baked->instance.transformID = transformID;
				baked->instance.instanceID = instanceCounter++;

				uint32_t meshID = baked->instance.meshID;
				meshes[meshID].worldAABB = Visibility::transformAABB(
					meshes[meshID].localAABB, bakedTransformsList[transformID]);

				node->instances.push_back(baked);

				fmt::print("  BakedInstance -> Node {} | TransformID {} | MeshID {} | MaterialID {} | Pass {}\n",
					nodeIndex, transformID, meshID, baked->instance.materialID,
					static_cast<int>(baked->passType));
			}
		}


		fmt::print("SceneGraph built: '{}'. Total bakedTransforms = {}\n\n",
			modelAsset.sceneName, bakedTransformsList.size());

		queue->push(context);
	}
}