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

static void printHierarchy(const std::shared_ptr<Node>& node, int depth = 0) {
	fmt::print("{}Node {}\n", std::string(depth * 2, ' '), reinterpret_cast<uintptr_t>(node.get()));

	for (auto& child : node->children)
		printHierarchy(child, depth + 1);
}

void SceneGraph::buildSceneGraph(ThreadContext& threadCtx, std::vector<GPUMeshData>& meshes) {
	ASSERT(threadCtx.workQueueActive != nullptr);
	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	ASSERT(queue);

	auto gltfJobs = queue->collect();
	for (auto& context : gltfJobs) {
		if (!context->isComplete()) continue;

		auto& gltf = context->gltfAsset;
		auto& file = *context->scene;

		fmt::print("Processing GLTF scene: {}\n", file.sceneName);

		std::vector<std::shared_ptr<Node>> nodes;
		nodes.reserve(gltf.nodes.size());

		std::unordered_map<uint32_t, std::vector<std::shared_ptr<RenderInstance>>> meshToInstances;
		for (const auto& inst : file.gpu.instances) {
			if (!inst) continue;
			meshToInstances[inst->gltfMeshIndex].push_back(inst);
		}

		for (size_t i = 0; i < gltf.nodes.size(); ++i) {
			uint32_t nodeIdx = static_cast<uint32_t>(i);
			auto& srcNode = gltf.nodes[nodeIdx];
			std::shared_ptr<Node> newNode;

			if (srcNode.meshIndex.has_value()) {
				auto meshIndex = *srcNode.meshIndex;
				newNode = std::make_shared<MeshNode>();
				auto& instances = meshToInstances[meshIndex];
				if (instances.empty()) {
					fmt::print("  Warning: MeshIndex {} has no matching instances.\n", meshIndex);
				}

				auto* meshNode = static_cast<MeshNode*>(newNode.get());
				for (auto& inst : instances) {
					meshNode->instances.push_back(inst);
				}

				fmt::print("  Node {} -> MeshIndex {}\n", nodeIdx, meshIndex);
			}

			else {
				newNode = std::make_shared<Node>();
				fmt::print("  Node {} -> Empty (No mesh)\n", nodeIdx);
			}

			newNode->nodeIndex = nodeIdx;

			std::visit(fastgltf::visitor{
				[&](const fastgltf::math::fmat4x4& matrix) {
					memcpy(&newNode->localTransform, matrix.data(), sizeof(matrix));
				},
				[&](const fastgltf::TRS& transform) {
					glm::vec3 tl(transform.translation[0], transform.translation[1], transform.translation[2]);
					glm::quat rot(transform.rotation[3], transform.rotation[0], transform.rotation[1], transform.rotation[2]);
					glm::vec3 sc(transform.scale[0], transform.scale[1], transform.scale[2]);

					fmt::print("    Translation: "); printVec3(tl); fmt::print("\n");
					fmt::print("    Scale: "); printVec3(sc); fmt::print("\n");

					glm::mat4 tm = glm::translate(glm::mat4(1.0f), tl);
					glm::mat4 rm = glm::toMat4(rot);
					glm::mat4 sm = glm::scale(glm::mat4(1.0f), sc);
					newNode->localTransform = tm * rm * sm;
				}
			}, srcNode.transform);

			nodes.push_back(newNode);
		}

		// Build parent-child hierarchy
		for (size_t i = 0; i < gltf.nodes.size(); ++i) {
			auto& children = gltf.nodes[i].children;
			for (auto childIdx : children) {
				nodes[i]->children.push_back(nodes[static_cast<uint32_t>(childIdx)]);
				nodes[static_cast<uint32_t>(childIdx)]->parent = nodes[i];
				fmt::print("  Parent {} -> Child {}\n", static_cast<uint32_t>(i), static_cast<uint32_t>(childIdx));
			}
		}

		file.scene.nodes = nodes;
		file.scene.topNodes.clear();

		for (auto& node : nodes) {
			if (node->parent.expired()) {
				file.scene.topNodes.push_back(node);
				node->refreshTransform(glm::mat4(1.0f));
				//printHierarchy(node);
			}
		}

		// Build final meshID -> modelMatrix map
		for (auto& node : file.scene.nodes) {
			auto meshNode = std::dynamic_pointer_cast<MeshNode>(node);
			for (auto& inst : meshNode->instances) {
				if (!inst) continue;
				uint32_t meshID = inst->instances->meshID;
				inst->instances->modelMatrix = meshNode->worldTransform;

				fmt::print("  MeshNode meshID {} -> worldMatrix:\n", meshID);
				printMat4(meshNode->worldTransform);

				const auto& local = meshes[meshID].localAABB;
				meshes[meshID].worldAABB = Visibility::transformAABB(local, meshNode->worldTransform);
			}
		}

		fmt::print("Finished scene graph build for '{}'\n", file.sceneName);
		queue->push(context);
	}
}



void ModelAsset::FindVisibleObjects(
	std::vector<GPUInstance>& outOpaqueVisibles,
	std::vector<GPUInstance>& outTransparentVisibles,
	const std::unordered_map<uint32_t, std::vector<glm::mat4>>& meshIDToTransformMap,
	const std::unordered_set<uint32_t>& visibleMeshIDSet)
{
	for (auto& node : scene.topNodes) {
		if (node) {
			node->FindVisibleObjects(outOpaqueVisibles, outTransparentVisibles, meshIDToTransformMap, visibleMeshIDSet);
		}
	}
}

void ModelAsset::bakeMeshNodeTransforms(
	const std::shared_ptr<MeshNode>& node,
	const glm::mat4& parentMatrix,
	std::unordered_map<uint32_t, std::vector<glm::mat4>>& outMeshTransforms)
{
	if (!node) return;

	glm::mat4 model = parentMatrix * node->localTransform;

	for (const auto& inst : node->instances) {
		if (!inst || !inst->instances) continue;

		uint32_t meshID = inst->instances->meshID;
		outMeshTransforms[meshID].push_back(model);
	}

	for (const auto& child : node->children) {
		auto meshChild = std::dynamic_pointer_cast<MeshNode>(child);
		if (meshChild) {
			bakeMeshNodeTransforms(meshChild, model, outMeshTransforms);
		}
	}
}

void MeshNode::FindVisibleObjects(
	std::vector<GPUInstance>& outOpaqueVisibles,
	std::vector<GPUInstance>& outTransparentVisibles,
	const std::unordered_map<uint32_t, std::vector<glm::mat4>>& meshIDToTransformMap,
	const std::unordered_set<uint32_t>& visibleMeshIDSet)
{
	for (const auto& inst : instances) {
		if (!inst || !inst->instances) continue;

		uint32_t meshID = inst->instances->meshID;
		uint32_t materialIndex = inst->instances->materialIndex;

		if (visibleMeshIDSet.contains(meshID)) {
			auto it = meshIDToTransformMap.find(meshID);
			ASSERT(it != meshIDToTransformMap.end());

			for (const auto& modelMatrix : it->second) {
				GPUInstance gpuInst{};
				gpuInst.meshID = meshID;
				gpuInst.materialIndex = materialIndex;
				gpuInst.modelMatrix = modelMatrix;

				fmt::print("Visible Object -> meshID: {}, materialIndex: {}, pass: {}\n",
					meshID, materialIndex,
					(inst->passType == MaterialPass::Transparent ? "Transparent" : "Opaque"));

				if (inst->passType == MaterialPass::Transparent)
					outTransparentVisibles.push_back(gpuInst);
				else
					outOpaqueVisibles.push_back(gpuInst);
			}
		}
	}

	for (auto& c : children) {
		if (c) c->FindVisibleObjects(outOpaqueVisibles, outTransparentVisibles, meshIDToTransformMap, visibleMeshIDSet);
	}
}




void ModelAsset::clearAll() {
	auto device = Backend::getDevice();
	auto allocator = Engine::getState().getGPUResources().getAllocator();

	Backend::getGraphicsQueue().waitIdle();

	// Don't free global images or samplers twice
	for (auto& img : gpu.images) {
		if (img.image == VK_NULL_HANDLE ||
			img.image == ResourceManager::getCheckboardTex().image ||
			img.image == ResourceManager::getWhiteImage().image ||
			img.image == ResourceManager::getMetalRoughImage().image ||
			img.image == ResourceManager::getAOImage().image ||
			img.image == ResourceManager::getNormalImage().image ||
			img.image == ResourceManager::getEmissiveImage().image) {
			continue;
		}

		RendererUtils::destroyImage(device, img, allocator);
	}

	for (auto& sampler : gpu.samplers) {
		if (sampler == VK_NULL_HANDLE ||
			sampler == ResourceManager::getDefaultSamplerLinear() ||
			sampler == ResourceManager::getDefaultSamplerNearest()) {
			continue;
		}

		vkDestroySampler(device, sampler, nullptr);
	}
}