#pragma once

#include "common/EngineTypes.h"
#include "gpu_types/PipelineManager.h"
#include "common/Vk_Types.h"
#include "Renderer.h"

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

namespace DrawPreperation {
	void uploadGPUBuffersForFrame(FrameContext& frameCtx, GPUQueue& transferQueue, const VmaAllocator allocator);

	void meshDataAndTransformsListUpload(
		FrameContext& frameCtx,
		MeshRegistry& meshes,
		const std::vector<glm::mat4>& transformsList,
		GPUQueue& transferQueue,
		const VmaAllocator allocator,
		bool uploadTransforms = false);


	void buildAndSortIndirectDraws(
		FrameContext& frameCtx,
		const std::vector<GPUDrawRange>& drawRanges,
		const std::vector<GPUMeshData>& meshes);
}