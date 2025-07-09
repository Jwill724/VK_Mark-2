#pragma once

#include "common/EngineTypes.h"
#include "common/Vk_Types.h"

class IRenderable {
public:
	virtual void FindVisibleInstances(
		std::vector<GPUInstance>& outVisibleOpaqueInstances,
		std::vector<GPUInstance>& outVisibleTransparentInstances,
		std::vector<glm::mat4>& outFrameTransformsList,
		const std::vector<glm::mat4>& bakedTransformsList,
		const std::unordered_set<uint32_t>& visibleMeshIDSet) = 0;

	virtual ~IRenderable() = default;
};

// ====== Scene Graph Node Base ======
struct Node : public IRenderable {
	std::weak_ptr<Node> parent;
	std::vector<std::shared_ptr<Node>> children;

	glm::mat4 localTransform{};
	glm::mat4 worldTransform{};

	uint32_t nodeID = UINT32_MAX;

	std::vector<std::shared_ptr<BakedInstance>> instances;

	void refreshTransform(const glm::mat4& parentMatrix) {
		worldTransform = parentMatrix * localTransform;
		for (auto& c : children) {
			if (c) c->refreshTransform(worldTransform);
		}
	}

	virtual void FindVisibleInstances(
		std::vector<GPUInstance>& outVisibleOpaqueInstances,
		std::vector<GPUInstance>& outVisibleTransparentInstances,
		std::vector<glm::mat4>& outFrameTransformsList,
		const std::vector<glm::mat4>& bakedTransformsList,
		const std::unordered_set<uint32_t>& visibleMeshIDSet
	) override {

		// === Traverse children first
		for (const auto& c : children) {
			if (c) c->FindVisibleInstances(
				outVisibleOpaqueInstances,
				outVisibleTransparentInstances,
				outFrameTransformsList,
				bakedTransformsList,
				visibleMeshIDSet);
		}

		// === Process current node's instances
		for (const auto& inst : instances) {
			fmt::print("\nCULLING: nodeID {}\n", inst->nodeID);

			if (!inst) continue;
			if (inst->nodeID >= bakedTransformsList.size()) continue;

			const uint32_t meshID = inst->instance.meshID;

			// Search for visible meshIDs
			if (visibleMeshIDSet.find(meshID) == visibleMeshIDSet.end())
				continue;

			if (inst->instance.transformID >= bakedTransformsList.size())
				continue;

			fmt::print("Found visible: Node ID: {}\n", inst->nodeID);

			outFrameTransformsList.push_back(bakedTransformsList[inst->instance.transformID]);

			GPUInstance gpuInst = {
				.instanceID = inst->instance.instanceID,
				.materialID = inst->instance.materialID,
				.meshID = meshID,
				.transformID = static_cast<uint32_t>(outFrameTransformsList.size() - 1)
			};

			if (inst->passType == MaterialPass::Transparent)
				outVisibleTransparentInstances.push_back(gpuInst);
			else
				outVisibleOpaqueInstances.push_back(gpuInst);
		}
	}
};

namespace SceneGraph {
	void buildSceneGraph(ThreadContext& threadCtx, std::vector<GPUMeshData>& meshes, std::vector<glm::mat4>& bakedTransformsList);
}