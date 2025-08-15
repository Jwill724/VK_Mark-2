#pragma once

#include "common/ResourceTypes.h"
#include "common/EngineTypes.h"

namespace SceneGraph {

	class IRenderable {
	public:
		virtual void FindVisibleInstances(
			std::vector<GPUInstance>& outVisibleOpaqueInstances,
			std::vector<GPUInstance>& outVisibleTransparentInstances,
			std::vector<glm::mat4>& outFrameTransformsList,
			const std::unordered_set<uint32_t> visibleMeshIDSet) = 0;

		virtual ~IRenderable() = default;
	};

	// ====== Scene Graph Node Base ======
	struct Node : public IRenderable {
		std::weak_ptr<Node> parent;
		std::vector<std::shared_ptr<Node>> children;

		glm::mat4 localTransform{ 1.0f };
		glm::mat4 worldTransform{ 1.0f };

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
			const std::unordered_set<uint32_t> visibleMeshIDSet
		) override {

			for (const auto& inst : instances) {
				if (!inst) continue;

				const uint32_t meshID = inst->instance.meshID;

				// Search for visible meshIDs
				if (visibleMeshIDSet.find(meshID) == visibleMeshIDSet.end())
					continue;

				outFrameTransformsList.push_back(worldTransform);

				GPUInstance gpuInst{
					.instanceID = inst->instance.instanceID,
					.materialID = inst->instance.materialID,
					.meshID = meshID,
					.transformID = static_cast<uint32_t>(outFrameTransformsList.size() - 1)
				};

				outVisibleOpaqueInstances.push_back(gpuInst);

				if (inst->passType == MaterialPass::Transparent)
					outVisibleTransparentInstances.push_back(gpuInst);
				else
					outVisibleOpaqueInstances.push_back(gpuInst);
			}

			for (const auto& c : children) {
				if (c) c->FindVisibleInstances(
					outVisibleOpaqueInstances,
					outVisibleTransparentInstances,
					outFrameTransformsList,
					visibleMeshIDSet);
			}
		}
	};

	void buildSceneGraph(ThreadContext& threadCtx, std::vector<GPUMeshData>& meshes);
}