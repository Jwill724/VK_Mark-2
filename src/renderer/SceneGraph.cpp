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
	assert(threadCtx.workQueueActive != nullptr);

	auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
	assert(queue);

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
				static_cast<MeshNode*>(newNode.get())->mesh = file.meshes[*node.meshIndex];
			}
			else {
				newNode = std::make_shared<Node>();
			}

			nodes.push_back(newNode);
			file.nodes = nodes;

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
				file.topNodes.push_back(node);
				node->refreshTransform(glm::mat4{ 1.f });
			}
		}

		queue->push(context);
	}
}

void LoadedGLTF::Draw(const glm::mat4& topMatrix, DrawContext& ctx) {
	// create renderables from the scenenodes
	for (auto& n : topNodes) {
		n->Draw(topMatrix, ctx);
	}
}

void LoadedGLTF::clearAll() {
	auto device = Backend::getDevice();

	auto allocator = Engine::getState().getGPUResources().getAllocator();

	Backend::getGraphicsQueue().waitIdle();

	// Don't free global images or samplers twice
	for (auto& img : images) {
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

	for (auto& sampler : samplers) {
		if (sampler == ResourceManager::getDefaultSamplerLinear() ||
			sampler == ResourceManager::getDefaultSamplerNearest()) {
			continue;
		}

		vkDestroySampler(device, sampler, nullptr);
	}
}

void MeshNode::Draw(const glm::mat4& topMatrix, DrawContext& ctx) {
	glm::mat4 nodeMatrix;
	for (auto& s : mesh->materialHandles) {
		nodeMatrix = topMatrix * worldTransform;

		RenderObject def{};
		def.passType = s->passType;
		def.drawRangeIndex = s->instance->drawRangeIndex;
		def.materialIndex = s->instance->materialIndex;
		def.modelMatrix = nodeMatrix;
		def.aabb = Visibility::transformAABB(s->instance->localAABB, nodeMatrix);

		if (s->passType == MaterialPass::Transparent) {
			if (Visibility::isVisible(def.aabb, ctx.frustum)) {
				ctx.TransparentSurfaces.push_back(def);
			}
		}
		else {
			if (Visibility::isVisible(def.aabb, ctx.frustum)) {
				ctx.OpaqueSurfaces.push_back(def);
			}
		}
	}

	// recurse down
	Node::Draw(topMatrix, ctx);
}