#include "pch.h"

#include "FrameContext.h"
#include "renderer/backend/Backend.h"
#include "utils/SyncUtils.h"
#include "utils/BufferUtils.h"
#include "renderer/gpu/Descriptor.h"
#include "renderer/gpu/CommandBuffer.h"

std::vector<std::unique_ptr<FrameContext>> initFrameContexts(
	const VkDevice device,
	const VkDescriptorSetLayout frameLayout,
	const VmaAllocator alloc,
	ResourceStats resStats,
	uint32_t& framesInFlight,
	bool isAssetsLoaded)
{
	auto& swapDef = Backend::getSwapchainDef();
	framesInFlight = swapDef.imageCount;

	std::vector<std::unique_ptr<FrameContext>> frameContexts;

	frameContexts.resize(framesInFlight);

	uint32_t graphicsIndex = Backend::getGraphicsQueue().familyIndex;
	uint32_t transferIndex = Backend::getTransferQueue().familyIndex;
	uint32_t computeIndex = Backend::getComputeQueue().familyIndex;

	size_t totalGPUStagingSize = 0;
	if (isAssetsLoaded) {
		totalGPUStagingSize =
			INSTANCE_SIZE_BYTES +
			INDIRECT_SIZE_BYTES +
			TRANSFORMS_SIZE_BYTES +
			sizeof(GPUAddressTable);
	}

	fmt::print("Frames in flight:[{}]\n", framesInFlight);

	for (uint32_t i = 0; i < framesInFlight; ++i) {
		auto frame = std::make_unique<FrameContext>();
		frame->frameIndex = i;

		frame->graphicsPool = CommandBuffer::createCommandPool(device, graphicsIndex);
		frame->transferPool = CommandBuffer::createCommandPool(device, transferIndex);
		frame->commandBuffer = CommandBuffer::createCommandBuffer(device, frame->graphicsPool);
		frame->set = DescriptorSetOverwatch::mainDescriptorManager.allocateDescriptor(device, frameLayout);

		if (GPU_ACCELERATION_ENABLED) {
			frame->computePool = CommandBuffer::createCommandPool(device, computeIndex);
		}

		if (isAssetsLoaded) {
			frame->addressTableBuffer = BufferUtils::createBuffer(
				sizeof(GPUAddressTable),
				VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
				VK_BUFFER_USAGE_TRANSFER_DST_BIT |
				VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
				VMA_MEMORY_USAGE_GPU_ONLY,
				alloc);

			frame->drawDataPC.totalVertexCount = resStats.totalVertexCount;
			frame->drawDataPC.totalIndexCount = resStats.totalIndexCount;
			frame->drawDataPC.totalMeshCount = resStats.totalMeshCount;
			frame->drawDataPC.totalMaterialCount = resStats.totalMaterialCount;

			frame->combinedGPUStaging = BufferUtils::createBuffer(
				totalGPUStagingSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
				alloc);
			ASSERT(frame->combinedGPUStaging.info.pMappedData);

			frame->visibleInstancesBuffer = BufferUtils::createGPUAddressBuffer(
				AddressBufferType::VisibleInstances, frame->addressTable, INSTANCE_SIZE_BYTES, alloc);
			frame->persistentGPUBuffers.push_back(frame->visibleInstancesBuffer);

			frame->indirectDrawsBuffer = BufferUtils::createGPUAddressBuffer(
				AddressBufferType::IndirectDraws, frame->addressTable, INDIRECT_SIZE_BYTES, alloc);
			frame->persistentGPUBuffers.push_back(frame->indirectDrawsBuffer);
		}

		frameContexts[i] = std::move(frame);
	}

	return frameContexts;
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

void FrameContext::writeFrameDescriptors(const VkDevice device) {
	descriptorWriter.clear();

	constexpr size_t offset = 0;

	// Only write the table if updated
	if (addressTableDirty) {
		descriptorWriter.writeBuffer(
			ADDRESS_TABLE_BINDING,
			addressTableBuffer.buffer,
			sizeof(GPUAddressTable),
			offset,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			set
		);

		addressTableDirty = false;
	}

	descriptorWriter.writeBuffer(
		FRAME_BINDING_SCENE,
		sceneDataBuffer.buffer,
		sizeof(GPUSceneData),
		offset,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		set
	);

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

		for (auto& buf : frame.persistentGPUBuffers)
			BufferUtils::destroyAllocatedBuffer(buf, alloc);

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

		if (frame.addressTableBuffer.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(frame.addressTableBuffer, alloc);
	}

	frameContexts.clear();
}