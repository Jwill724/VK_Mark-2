#pragma once

#include "renderer/Renderer.h"
#include "renderer/scene/RenderScene.h"

namespace DrawPreparation {
	void uploadGPUBuffersForFrame(
		FrameContext& frameCtx,
		GPUResources& gpuResources,
		const std::vector<glm::mat4>& transforms,
		const std::vector<LocalLight>& lights,
		GPUQueue& transferQueue,
		bool isTemporalValid,
		bool isGPUAccelOn);

	void buildAndSortIndirectDraws(
		FrameContext& frameCtx,
		const std::vector<Mesh>& meshes,
		const std::vector<MeshLODs>& meshLods,
		const std::vector<AABB>& worldAABBs,
		const glm::vec4& cameraPos,
		const glm::mat4& cameraProj,
		const RenderToggles& dbg);

	bool syncGlobalInstancesAndTransforms(
		std::unordered_map<SceneID, SceneProfileEntry>& sceneProfiles,
		std::vector<VirtualInstance>& globalInstances,
		std::vector<glm::mat4>& globalTransforms,
		const double deltaTime);
}
