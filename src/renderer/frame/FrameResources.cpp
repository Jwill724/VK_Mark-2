#include "pch.h"

#include "FrameResources.h"
#include "renderer/backend/memory/AllocatedBuffer.h"
#include "renderer/backend/memory/Budgets.h"

void Cmaa2BufferSizes::UpdateCmaa2BufferSizes(
	const uint32_t extentWidth,
	const uint32_t extentHeight)
{
	pixelCount = extentWidth * extentHeight;

	quadCountX = (extentWidth + 1u) / 2u;
	quadCountY = (extentHeight + 1u) / 2u;
	quadCount  = quadCountX * quadCountY;

	controlBytes = 64u;

	shapeCandidatesBytes =
		static_cast<size_t>(pixelCount) * sizeof(uint32_t);

	deferredLocationsBytes =
		static_cast<size_t>(quadCount) * sizeof(uint32_t);

	deferredHeadsBytes =
		static_cast<size_t>(quadCount) * sizeof(uint32_t);

	deferredItemsCapacity = pixelCount * 2u;

	deferredItemsBytes =
		static_cast<size_t>(deferredItemsCapacity) *
		sizeof(uint32_t) * 4u;

	// alignment (final stage only)
	controlBytes = AllocatedBuffer::AlignUp(controlBytes, MIN_SSBO_SIZE_GPU_BYTES);

	shapeCandidatesBytes =
		AllocatedBuffer::AlignUp(shapeCandidatesBytes, MIN_SSBO_SIZE_GPU_BYTES);

	deferredLocationsBytes =
		AllocatedBuffer::AlignUp(deferredLocationsBytes, MIN_SSBO_SIZE_GPU_BYTES);

	deferredHeadsBytes =
		AllocatedBuffer::AlignUp(deferredHeadsBytes, MIN_SSBO_SIZE_GPU_BYTES);

	deferredItemsBytes =
		AllocatedBuffer::AlignUp(deferredItemsBytes, MIN_SSBO_SIZE_GPU_BYTES);
}

void ClusterBufferSizes::UpdateClusterBufferSizes(
	uint32_t screenWidth,
	uint32_t screenHeight,
	uint32_t tileSizeX,
	uint32_t tileSizeY,
	uint32_t zSlices)
{
	// --- derived grid sizes ---
	tileCountX = (screenWidth + tileSizeX - 1u) / tileSizeX;
	tileCountY = (screenHeight + tileSizeY - 1u) / tileSizeY;

	tileCount = tileCountX * tileCountY;
	clusterCount = tileCount * zSlices;

	// --- per-cluster buffers ---
	clusterCountsBytes =
		static_cast<size_t>(clusterCount) * sizeof(uint32_t);

	clusterOffsetsBytes =
		static_cast<size_t>(clusterCount) * sizeof(uint32_t);

	clusterCursorsBytes =
		static_cast<size_t>(clusterCount) * sizeof(uint32_t);

	clusterLightIDsBytes =
		static_cast<size_t>(clusterCount) *
		static_cast<size_t>(MAX_LIGHTS_PER_CLUSTER) *
		sizeof(uint32_t);

	// --- tile-level Hi-Z slice ranges ---
	clusterTileSliceRangesBytes =
		static_cast<size_t>(tileCount) * sizeof(glm::uvec2); 
		// (better than raw 8u magic)

	// --- scan scratch ---
	const uint32_t elementsPerBlock = 256u;
	const uint32_t blockCount =
		(clusterCount + elementsPerBlock - 1u) / elementsPerBlock;

	clusterScanScratchBytes = 4u;

	const uint32_t alignment = 256u;

	clusterCountsBytes =
		AllocatedBuffer::AlignUp(clusterCountsBytes, alignment);

	clusterOffsetsBytes =
		AllocatedBuffer::AlignUp(clusterOffsetsBytes, alignment);

	clusterCursorsBytes =
		AllocatedBuffer::AlignUp(clusterCursorsBytes, alignment);

	clusterLightIDsBytes =
		AllocatedBuffer::AlignUp(clusterLightIDsBytes, alignment);

	clusterTileSliceRangesBytes =
		AllocatedBuffer::AlignUp(clusterTileSliceRangesBytes, alignment);

	clusterScanScratchBytes =
		AllocatedBuffer::AlignUp(clusterScanScratchBytes, alignment);
}
