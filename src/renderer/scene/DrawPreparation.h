#pragma once

#include "common/EngineTypes.h"
#include "renderer/Renderer.h"
#include "SceneGraph.h"

struct OpaqueBatchKey {
	uint32_t meshID;
	uint32_t materialID;

	bool operator==(const OpaqueBatchKey& other) const {
		return meshID == other.meshID && materialID == other.materialID;
	}
};

struct OpaqueBatchKeyHash {
	std::size_t operator()(const OpaqueBatchKey& k) const {
		std::size_t h1 = std::hash<uint32_t>{}(k.meshID);
		std::size_t h2 = std::hash<uint32_t>{}(k.materialID);
		return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
	}
};

namespace DrawPreparation {
	void uploadGPUBuffersForFrame(FrameContext& frameCtx, GPUQueue& transferQueue);

	void buildAndSortIndirectDraws(
		FrameContext& frameCtx,
		const std::vector<GPUMeshData>& meshes,
		const std::vector<AABB>& worldAABBs,
		const glm::vec4 cameraPos);

	void syncGlobalInstancesAndTransforms(
		FrameContext& frameCtx,
		GPUResources& gpuResources,
		std::unordered_map<SceneID, SceneProfileEntry>& sceneProfiles,
		std::vector<GlobalInstance>& globalInstances,
		std::vector<glm::mat4>& globalTransforms,
		GPUQueue& transferQueue);
}