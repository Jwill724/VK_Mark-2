#include "pch.h"

#include "FrameResources.h"
#include "../backend/memory/AllocatedBuffer.h"
#include "../backend/memory/Budgets.h"

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

	controlBytes = AllocatedBuffer::AlignUp(controlBytes, MIN_SSBO_ALIGNMENT_BYTES);

	shapeCandidatesBytes =
		AllocatedBuffer::AlignUp(shapeCandidatesBytes, MIN_SSBO_ALIGNMENT_BYTES);

	deferredLocationsBytes =
		AllocatedBuffer::AlignUp(deferredLocationsBytes, MIN_SSBO_ALIGNMENT_BYTES);

	deferredHeadsBytes =
		AllocatedBuffer::AlignUp(deferredHeadsBytes, MIN_SSBO_ALIGNMENT_BYTES);

	deferredItemsBytes =
		AllocatedBuffer::AlignUp(deferredItemsBytes, MIN_SSBO_ALIGNMENT_BYTES);
}

void ClusterBufferSizes::UpdateClusterBufferSizes(
	uint32_t screenWidth,
	uint32_t screenHeight,
	uint32_t tileSizeX,
	uint32_t tileSizeY,
	uint32_t zSlices)
{
	tileCountX = (screenWidth + tileSizeX - 1u) / tileSizeX;
	tileCountY = (screenHeight + tileSizeY - 1u) / tileSizeY;

	tileCount = tileCountX * tileCountY;
	clusterCount = tileCount * zSlices;

	clusterCountsBytes =
		static_cast<size_t>(clusterCount) * sizeof(uint32_t);

	clusterOffsetsBytes =
		static_cast<size_t>(clusterCount) * sizeof(uint32_t);

	clusterCursorsBytes =
		static_cast<size_t>(clusterCount) * sizeof(uint32_t);

	clusterLightIDsBytes =
		static_cast<size_t>(clusterCount) *
		static_cast<size_t>(RD::MAX_LIGHTS_PER_CLUSTER) *
		sizeof(uint32_t);

	clusterTileSliceRangesBytes = static_cast<size_t>(tileCount) * sizeof(glm::uvec2); 

	const uint32_t elementsPerBlock = 256u;
	const uint32_t blockCount =
		(clusterCount + elementsPerBlock - 1u) / elementsPerBlock;

	clusterScanScratchBytes = 4u;

	clusterCountsBytes =
		AllocatedBuffer::AlignUp(clusterCountsBytes, MIN_SSBO_ALIGNMENT_BYTES);

	clusterOffsetsBytes =
		AllocatedBuffer::AlignUp(clusterOffsetsBytes, MIN_SSBO_ALIGNMENT_BYTES);

	clusterCursorsBytes =
		AllocatedBuffer::AlignUp(clusterCursorsBytes, MIN_SSBO_ALIGNMENT_BYTES);

	clusterLightIDsBytes =
		AllocatedBuffer::AlignUp(clusterLightIDsBytes, MIN_SSBO_ALIGNMENT_BYTES);

	clusterTileSliceRangesBytes =
		AllocatedBuffer::AlignUp(clusterTileSliceRangesBytes, MIN_SSBO_ALIGNMENT_BYTES);

	clusterScanScratchBytes =
		AllocatedBuffer::AlignUp(clusterScanScratchBytes, MIN_SSBO_ALIGNMENT_BYTES);
}
