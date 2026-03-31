#include "pch.h"

#include "FrameContext.h"
#include "renderer/backend/Backend.h"
#include "utils/SyncUtils.h"
#include "utils/BufferUtils.h"
#include "renderer/gpu/CommandBuffer.h"
#include "renderer/scene/LightingSystem.h"

std::vector<std::unique_ptr<FrameContext>> initFrameContexts(
	const VkDevice device,
	const VkDescriptorSetLayout frameLayout,
	const VmaAllocator alloc,
	uint32_t& framesInFlight)
{
	auto& swapDef = Backend::getSwapchainDef();
	framesInFlight = swapDef.imageCount;

	std::vector<std::unique_ptr<FrameContext>> frameContexts;

	frameContexts.resize(framesInFlight);

	uint32_t graphicsIndex = Backend::getGraphicsQueue().familyIndex;
	uint32_t transferIndex = Backend::getTransferQueue().familyIndex;
	uint32_t computeIndex = Backend::getComputeQueue().familyIndex;

	size_t totalGPUStagingSize =
		MAX_GPU_INSTANCE_SIZE_BYTES +
		MAX_GPU_INDIRECT_SIZE_BYTES +
		sizeof(GPUAddressTable);

	fmt::print("Frames in flight:[{}]\n", framesInFlight);

	for (uint32_t i = 0; i < framesInFlight; ++i) {
		auto frame = std::make_unique<FrameContext>();
		frame->frameIndex = i;

		frame->graphicsPool = CommandBuffer::createCommandPool(device, graphicsIndex);
		frame->cmdBuffer = CommandBuffer::createCommandBuffer(device, frame->graphicsPool);
		frame->set = DescriptorSetOverwatch::mainDescriptorManager.allocateDescriptor(device, frameLayout);

		frame->transferPool = CommandBuffer::createCommandPool(device, transferIndex);

		if (Backend::isComputeAvailable()) {
			frame->computePool = CommandBuffer::createCommandPool(device, computeIndex);
		}

		frame->addressTable_GPU = BufferUtils::createBuffer(
			sizeof(GPUAddressTable),
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT |
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VMA_MEMORY_USAGE_GPU_ONLY,
			alloc);

		frame->combinedGPUStaging = BufferUtils::createBuffer(
			totalGPUStagingSize,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
			alloc);
		ASSERT(frame->combinedGPUStaging.info.pMappedData);

		frame->visibleInstances_GPU = BufferUtils::createGPUAddressBuffer(
			AddressBufferType::VisibleInstances,
			frame->addressTable,
			MAX_GPU_INSTANCE_SIZE_BYTES,
			alloc);
		frame->persistentGPUBuffers.push_back(frame->visibleInstances_GPU);

		frame->indirectDraws_GPU = BufferUtils::createGPUAddressBuffer(
			AddressBufferType::IndirectDraws,
			frame->addressTable,
			MAX_GPU_INDIRECT_SIZE_BYTES,
			alloc);
		frame->persistentGPUBuffers.push_back(frame->indirectDraws_GPU);

		frame->visibleLightCount_GPU = BufferUtils::createGPUAddressBuffer(
			AddressBufferType::VisibleLightCount,
			frame->addressTable,
			256u,
			alloc
		);
		frame->persistentGPUBuffers.push_back(frame->visibleLightCount_GPU);

		frame->visibleLightIDs_GPU = BufferUtils::createGPUAddressBuffer(
			AddressBufferType::VisibleLightIDs,
			frame->addressTable,
			static_cast<size_t>(MAX_VISIBLE_LIGHTS) * sizeof(uint32_t),
			alloc
		);
		frame->persistentGPUBuffers.push_back(frame->visibleLightIDs_GPU);

		frame->dispatchIndirectArgs_GPU = BufferUtils::createGPUAddressBuffer(
			AddressBufferType::DispatchIndirectArgs,
			frame->addressTable,
			256u,
			alloc
		);
		frame->persistentGPUBuffers.push_back(frame->dispatchIndirectArgs_GPU);

		frame->transforms_GPU = BufferUtils::createGPUAddressBuffer(
			AddressBufferType::Transforms,
			frame->addressTable,
			TRANSFORMS_SIZE_BYTES,
			alloc
		);
		frame->persistentGPUBuffers.push_back(frame->transforms_GPU);

		frame->prevTransforms_GPU = BufferUtils::createGPUAddressBuffer(
			AddressBufferType::PrevTransforms,
			frame->addressTable,
			TRANSFORMS_SIZE_BYTES,
			alloc
		);
		frame->persistentGPUBuffers.push_back(frame->prevTransforms_GPU);

		frame->lights_GPU = BufferUtils::createGPUAddressBuffer(
			AddressBufferType::Lights,
			frame->addressTable,
			MAX_GPU_LIGHTS_SIZE_BYTES,
			alloc
		);
		frame->persistentGPUBuffers.push_back(frame->lights_GPU);


		VkQueryPoolCreateInfo queryPoolInfo{};
		queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
		queryPoolInfo.queryCount = TIMESTAMP_QUERY_COUNT;

		VK_CHECK(vkCreateQueryPool(
			device,
			&queryPoolInfo,
			nullptr,
			&frame->graphicsTimestampPool
		));

		for (uint32_t passIndex = 0; passIndex < TIMESTAMP_PASS_COUNT; ++passIndex) {
			frame->passTimestampRanges[passIndex].beginQuery = passIndex * 2;
			frame->passTimestampRanges[passIndex].endQuery = passIndex * 2 + 1;
		}

		frame->timestampResults.fill(0);

		frameContexts[i] = std::move(frame);
	}

	return frameContexts;
}

void FrameContext::createClusterBuffers(
	const uint32_t extentWidth,
	const uint32_t extentHeight,
	const VmaAllocator alloc)
{
	ClusterBufferSizes newClusterSizes;
	newClusterSizes = LightingSystem::computeClusterBufferSizes(
		extentWidth,
		extentHeight,
		clustered_UBO,
		alloc);
	clusterWriteNeeded = true;

	for (auto& buf : clusterGPUBuffers) {
		if (buf.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(buf, alloc);
	}
	clusterReset();

	clusterCounts_GPU = BufferUtils::createGPUAddressBuffer(
		AddressBufferType::ClusterCounts,
		addressTable,
		newClusterSizes.clusterCountsBytes,
		alloc
	);
	clusterGPUBuffers.push_back(clusterCounts_GPU);

	clusterOffsets_GPU = BufferUtils::createGPUAddressBuffer(
		AddressBufferType::ClusterOffsets,
		addressTable,
		newClusterSizes.clusterOffsetsBytes,
		alloc
	);
	clusterGPUBuffers.push_back(clusterOffsets_GPU);

	clusterCursors_GPU = BufferUtils::createGPUAddressBuffer(
		AddressBufferType::ClusterCursors,
		addressTable,
		newClusterSizes.clusterCursorsBytes,
		alloc
	);
	clusterGPUBuffers.push_back(clusterCursors_GPU);

	clusterLightIDs_GPU = BufferUtils::createGPUAddressBuffer(
		AddressBufferType::ClusterLightIDs,
		addressTable,
		newClusterSizes.clusterLightIDsBytes,
		alloc
	);
	clusterGPUBuffers.push_back(clusterLightIDs_GPU);

	clusterTileSliceRanges_GPU = BufferUtils::createGPUAddressBuffer(
		AddressBufferType::ClusterTileSliceRanges,
		addressTable,
		newClusterSizes.clusterTileSliceRangesBytes,
		alloc
	);
	clusterGPUBuffers.push_back(clusterTileSliceRanges_GPU);

	clusterScanScratch_GPU = BufferUtils::createGPUAddressBuffer(
		AddressBufferType::ClusterScanScratch,
		addressTable,
		newClusterSizes.clusterScanScratchBytes,
		alloc
	);
	clusterGPUBuffers.push_back(clusterScanScratch_GPU);
}

void FrameContext::createCMAA2Buffers(
	const uint32_t extentWidth,
	const uint32_t extentHeight,
	const VmaAllocator alloc)
{
	for (auto& buffer : cmaa2GPUBuffers) {
		if (buffer.buffer != VK_NULL_HANDLE) {
			BufferUtils::destroyAllocatedBuffer(buffer, alloc);
		}
	}
	cmaa2Reset();

	const uint32_t pixelCount = extentWidth * extentHeight;

	const uint32_t quadCountX = (extentWidth + 1u) / 2u;
	const uint32_t quadCountY = (extentHeight + 1u) / 2u;
	const uint32_t quadCount = quadCountX * quadCountY;

	size_t cmaa2ControlBytes = 64u;

	size_t cmaa2ShapeCandidatesBytes = static_cast<size_t>(pixelCount) * sizeof(uint32_t);
	size_t cmaa2DeferredLocationsBytes = static_cast<size_t>(quadCount) * sizeof(uint32_t);
	size_t cmaa2DeferredHeadsBytes = static_cast<size_t>(quadCount) * sizeof(uint32_t);

	const uint32_t deferredItemsCapacity = pixelCount * 2u;
	size_t cmaa2DeferredItemsBytes =
		static_cast<size_t>(deferredItemsCapacity) * sizeof(uint32_t) * 4u;

	cmaa2ControlBytes = BufferUtils::alignUp(cmaa2ControlBytes, 256u);
	cmaa2ShapeCandidatesBytes = BufferUtils::alignUp(cmaa2ShapeCandidatesBytes, 256u);
	cmaa2DeferredLocationsBytes = BufferUtils::alignUp(cmaa2DeferredLocationsBytes, 256u);
	cmaa2DeferredItemsBytes = BufferUtils::alignUp(cmaa2DeferredItemsBytes, 256u);
	cmaa2DeferredHeadsBytes = BufferUtils::alignUp(cmaa2DeferredHeadsBytes, 256u);

	cmaa2Push.halfWidth = quadCountX;
	cmaa2Push.maxShapeCandidates = pixelCount;
	cmaa2Push.maxDeferredItems = deferredItemsCapacity;
	cmaa2Push.maxDeferredLocations = quadCount;

	cmaa2Control_GPU = BufferUtils::createGPUAddressBuffer(
		AddressBufferType::Cmaa2Control,
		addressTable,
		cmaa2ControlBytes,
		alloc
	);
	cmaa2GPUBuffers.push_back(cmaa2Control_GPU);

	cmaa2ShapeCandidates_GPU = BufferUtils::createGPUAddressBuffer(
		AddressBufferType::Cmaa2ShapeCandidates,
		addressTable,
		cmaa2ShapeCandidatesBytes,
		alloc
	);
	cmaa2GPUBuffers.push_back(cmaa2ShapeCandidates_GPU);

	cmaa2DeferredLocations_GPU = BufferUtils::createGPUAddressBuffer(
		AddressBufferType::Cmaa2DeferredLocations,
		addressTable,
		cmaa2DeferredLocationsBytes,
		alloc
	);
	cmaa2GPUBuffers.push_back(cmaa2DeferredLocations_GPU);

	cmaa2DeferredItems_GPU = BufferUtils::createGPUAddressBuffer(
		AddressBufferType::Cmaa2DeferredItems,
		addressTable,
		cmaa2DeferredItemsBytes,
		alloc
	);
	cmaa2GPUBuffers.push_back(cmaa2DeferredItems_GPU);

	cmaa2DeferredHeads_GPU = BufferUtils::createGPUAddressBuffer(
		AddressBufferType::Cmaa2DeferredHeads,
		addressTable,
		cmaa2DeferredHeadsBytes,
		alloc
	);
	cmaa2GPUBuffers.push_back(cmaa2DeferredHeads_GPU);
}

void FrameContext::collectAndAppendCmds(std::vector<VkCommandBuffer>&& cmds, QueueType queue) {
	if (cmds.empty()) return;
	std::scoped_lock lock(submitMutex);

	auto& dstCmds = (queue == QueueType::Transfer) ? transferCmds
		: (queue == QueueType::Compute) ? computeCmds
		: secondaryCmds;
	dstCmds.insert(dstCmds.end(),
		std::make_move_iterator(cmds.begin()),
		std::make_move_iterator(cmds.end()));
}

void FrameContext::stashSubmitted(QueueType queue) {
	std::scoped_lock lock(submitMutex);

	auto& srcCmds = (queue == QueueType::Transfer) ? transferCmds
		: (queue == QueueType::Compute) ? computeCmds
		: secondaryCmds;
	auto& dstCmds = (queue == QueueType::Transfer) ? transferCmdsToFree
		: (queue == QueueType::Compute) ? computeCmdsToFree
		: secondaryCmdsToFree;
	dstCmds.insert(dstCmds.end(), srcCmds.begin(), srcCmds.end());
	srcCmds.clear();
}

void FrameContext::freeStashedCmds(const VkDevice device) {
	if (!transferCmdsToFree.empty()) {
		vkFreeCommandBuffers(device, transferPool,
			static_cast<uint32_t>(transferCmdsToFree.size()),
			transferCmdsToFree.data());
		transferCmdsToFree.clear();
	}
	if (!computeCmdsToFree.empty()) {
		vkFreeCommandBuffers(device, computePool,
			static_cast<uint32_t>(computeCmdsToFree.size()),
			computeCmdsToFree.data());
		computeCmdsToFree.clear();
	}
	if (!secondaryCmdsToFree.empty()) {
		vkFreeCommandBuffers(device, graphicsPool,
			static_cast<uint32_t>(secondaryCmdsToFree.size()),
			secondaryCmdsToFree.data());
		secondaryCmdsToFree.clear();
	}
}

void FrameContext::updateAddressTableIfDirty(const VkDevice device) {
	if (addressTable.isTableDirty()) {
		descriptorWriter.writeBuffer(
			ADDRESS_TABLE_BINDING,
			addressTable_GPU.buffer,
			sizeof(GPUAddressTable),
			0,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			set);

		addressTable.clearTableDirty();
	}
}

void FrameContext::writeFrameUniforms(const VkDevice device) {
	constexpr size_t offset = 0;

	if (visibleCount > 0) {
		descriptorWriter.writeBuffer(
			FRAME_BINDING_CSM,
			shadowCSM_UBO.buffer,
			sizeof(GPUShadowCSM),
			offset,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			set
		);
	}

	descriptorWriter.writeBuffer(
		FRAME_BINDING_SCENE,
		sceneData_UBO.buffer,
		sizeof(GPUSceneData),
		offset,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		set
	);

	if (clusterWriteNeeded) {
		descriptorWriter.writeBuffer(
			FRAME_BINDING_CLUSTERED,
			clustered_UBO.buffer,
			sizeof(LightingSystem::ClusteredData),
			offset,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			set
		);
		clusterWriteNeeded = false;
	}
}

void FrameContext::updateFrameSet(const VkDevice device) {
	descriptorWriter.updateSet(device, set);
}

void cleanupFrameContexts(
	std::vector<std::unique_ptr<FrameContext>>& frameContexts,
	const VkDevice device,
	const VmaAllocator alloc)
{
	for (auto& framePtr : frameContexts) {
		if (!framePtr) continue;

		auto& frame = *framePtr;

		frame.cpuDeletion.flush();

		for (uint32_t i = 0; i < static_cast<uint32_t>(AddressBufferType::Count); ++i) {
			AddressBufferType bufferType = static_cast<AddressBufferType>(i);
			frame.addressTable.removeAddress(bufferType);
		}

		for (auto& buf : frame.persistentGPUBuffers)
			BufferUtils::destroyAllocatedBuffer(buf, alloc);

		if (frame.clustered_UBO.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(frame.clustered_UBO, alloc);

		for (auto& buf : frame.clusterGPUBuffers) {
			if (buf.buffer != VK_NULL_HANDLE)
				BufferUtils::destroyAllocatedBuffer(buf, alloc);
		}
		frame.clusterReset();

		for (auto& buf : frame.cmaa2GPUBuffers) {
			if (buf.buffer != VK_NULL_HANDLE)
				BufferUtils::destroyAllocatedBuffer(buf, alloc);
		}
		frame.cmaa2Reset();

		if (frame.graphicsTimestampPool != VK_NULL_HANDLE) {
			vkDestroyQueryPool(device, frame.graphicsTimestampPool, nullptr);
		}

		frame.freeStashedCmds(device);

		frame.transferCmds.clear();
		frame.computeCmds.clear();
		frame.secondaryCmds.clear();

		if (frame.graphicsPool != VK_NULL_HANDLE)
			vkDestroyCommandPool(device, frame.graphicsPool, nullptr);

		if (frame.transferPool != VK_NULL_HANDLE)
			vkDestroyCommandPool(device, frame.transferPool, nullptr);

		if (frame.computePool != VK_NULL_HANDLE)
			vkDestroyCommandPool(device, frame.computePool, nullptr);

		if (frame.combinedGPUStaging.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(frame.combinedGPUStaging, alloc);

		if (frame.addressTable_GPU.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(frame.addressTable_GPU, alloc);
	}

	frameContexts.clear();
}
