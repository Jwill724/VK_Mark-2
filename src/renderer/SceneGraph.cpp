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

void SceneGraph::buildSceneGraph(ThreadContext& threadCtx) {
	ASSERT(threadCtx.workQueueActive != nullptr);

	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	ASSERT(queue && "[buildSceneGraph] queue broke.");

	auto gltfJobs = queue->collect();
	for (auto& context : gltfJobs) {
		if (!context->isComplete()) continue;

		auto& gltf = context->gltfAsset;
		auto& file = *context->scene;

		std::vector<std::shared_ptr<Node>> nodes;
		for (fastgltf::Node& node : gltf.nodes) {
			std::shared_ptr<Node> newNode;

			// find if the node has a mesh, and if it does hook it to the mesh pointer and allocate it with the MeshNode struct
			if (node.meshIndex.has_value()) {
				newNode = std::make_shared<MeshNode>();
				static_cast<MeshNode*>(newNode.get())->objs = file.gpu.sceneObjs[*node.meshIndex];
			}
			else {
				newNode = std::make_shared<Node>();
			}

			nodes.push_back(newNode);
			file.scene.nodes = nodes;

			// load the GLTF transform data, and convert it into a gltf final transform matrix
			std::visit(fastgltf::visitor{
				[&](const fastgltf::math::fmat4x4& matrix) {
					memcpy(&newNode->localTransform, matrix.data(), sizeof(matrix));
				},
				[&](const fastgltf::TRS& transform) {
					glm::vec3 tl(transform.translation[0], transform.translation[1], transform.translation[2]);
					glm::quat rot(transform.rotation[3], transform.rotation[0], transform.rotation[1], transform.rotation[2]);
					glm::vec3 sc(transform.scale[0], transform.scale[1], transform.scale[2]);

					glm::mat4 tm = glm::translate(glm::mat4(1.f), tl);
					glm::mat4 rm = glm::toMat4(rot);
					glm::mat4 sm = glm::scale(glm::mat4(1.f), sc);

					newNode->localTransform = tm * rm * sm;
				}
			}, node.transform);
		}

		// run loop again to setup transform hierarchy
		for (int i = 0; i < gltf.nodes.size(); i++) {
			fastgltf::Node& node = gltf.nodes[i];
			std::shared_ptr<Node>& sceneNode = nodes[i];

			for (auto& c : node.children) {
				sceneNode->children.push_back(nodes[c]);
				nodes[c]->parent = sceneNode;
			}
		}

		// find the top nodes, with no parents
		for (auto& node : nodes) {
			if (node->parent.lock() == nullptr) {
				file.scene.topNodes.push_back(node);
				node->refreshTransform(glm::mat4{ 1.f });
			}
		}

		queue->push(context);
	}
}

void LoadedGLTF::FlattenSceneToTransformList(
	const glm::mat4& topMatrix,
	std::unordered_map<uint32_t, glm::mat4>& meshIDToTransformMap)
{
	for (auto& n : scene.topNodes) {
		n->FlattenSceneToTransformList(topMatrix, meshIDToTransformMap);
	}
}

void LoadedGLTF::FindVisibleObjects(
	std::vector<GPUInstance>& outOpaqueVisibles,
	std::vector<GPUInstance>& outTransparentVisibles,
	const std::unordered_map<uint32_t, glm::mat4>& meshIDToTransformMap,
	const std::unordered_set<uint32_t>& visibleMeshIDSet) {
	for (auto& n : scene.topNodes) {
		n->FindVisibleObjects(outOpaqueVisibles, outTransparentVisibles, meshIDToTransformMap, visibleMeshIDSet);
	}
}

void LoadedGLTF::clearAll() {
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
		if (sampler == ResourceManager::getDefaultSamplerLinear() ||
			sampler == ResourceManager::getDefaultSamplerNearest()) {
			continue;
		}

		vkDestroySampler(device, sampler, nullptr);
	}
}

void MeshNode::FlattenSceneToTransformList(const glm::mat4& topMatrix,
	std::unordered_map<uint32_t, glm::mat4>& meshIDToTransformMap) {

	if (!objs || !objs->instances) return;

	glm::mat4 nodeMatrix = topMatrix * worldTransform;
	uint32_t meshID = objs->instances->meshID;

	meshIDToTransformMap[meshID] = nodeMatrix;

	for (auto& child : children) {
		if (child) {
			child->FlattenSceneToTransformList(topMatrix, meshIDToTransformMap);
		}
	}
}

void MeshNode::FindVisibleObjects(
	std::vector<GPUInstance>& outOpaqueVisibles,
	std::vector<GPUInstance>& outTransparentVisibles,
	const std::unordered_map<uint32_t, glm::mat4>& meshIDToTransformMap,
	const std::unordered_set<uint32_t>& visibleMeshIDSet)
{
	if (!objs || !objs->instances) return;

	const uint32_t meshID = objs->instances->meshID;
	// Only proceed if this node’s mesh is in the visible set
	if (visibleMeshIDSet.contains(meshID)) {
		auto it = meshIDToTransformMap.find(meshID);
		ASSERT(it != meshIDToTransformMap.end());

		GPUInstance inst{};
		inst.meshID = meshID;
		inst.materialIndex = objs->instances->materialIndex;
		inst.modelMatrix = it->second;

		if (objs->pass == MaterialPass::Transparent)
			outTransparentVisibles.push_back(inst);
		else
			outOpaqueVisibles.push_back(inst);
	}

	for (auto& child : children) {
		if (child) {
			child->FindVisibleObjects(outOpaqueVisibles, outTransparentVisibles, meshIDToTransformMap, visibleMeshIDSet);
		}
	}
}