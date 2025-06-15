#pragma once

#include "common/EngineTypes.h"
#include "gpu/PipelineManager.h"
#include "common/Vk_Types.h"
#include "Renderer.h"

struct BatchKey {
	MaterialPass passType;
	uint32_t materialIndex;
	uint32_t drawRangeIndex;

	bool operator==(const BatchKey& other) const {
		return passType == other.passType &&
			materialIndex == other.materialIndex;
	}
};

struct BatchKeyHash {
	std::size_t operator()(const BatchKey& k) const {
		std::size_t h1 = std::hash<uint8_t>{}(static_cast<uint8_t>(k.passType));
		std::size_t h2 = std::hash<uint32_t>{}(k.materialIndex);
		return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
	}
};

namespace Batching {
	void buildAndSortBatches(
		const std::vector<RenderObject>& opaqueObjects,
		const std::vector<RenderObject>& transparentObjects,
		const std::vector<GPUDrawRange>& drawRanges,
		std::vector<SortedBatch>& outSortedOpaqueBatches,
		std::vector<RenderObject>& outSortedTransparent);

	GraphicsPipeline* getPipelineForPass(MaterialPass pass);

	void buildInstanceBuffer(const std::vector<RenderObject>& objects, FrameContext& frame, GPUResources& resources);

	void createIndirectCommandBuffer(const std::vector<RenderObject>& objects,
		FrameContext& frame,
		GPUResources& resources);

	void uploadBuffersForFrame(FrameContext& frame, GPUResources& resources, GPUQueue& transferQueue);
}