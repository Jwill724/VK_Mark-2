#include "pch.h"

#include "FrameContext.h"
#include "renderer/backend/Backend.h"
#include "utils/SyncUtils.h"
#include "utils/BufferUtils.h"
#include "renderer/gpu/Descriptor.h"
#include "renderer/gpu/CommandBuffer.h"

using namespace FrameContext;

std::vector<std::unique_ptr<FrameCtx>> FrameContext::initFrameContexts(
	const VkDevice device,
	const VkDescriptorSetLayout frameLayout,
	const VmaAllocator alloc,
	ResourceStats resStats,
	TimelineSync transferSync,
	TimelineSync computeSync,
	uint32_t& framesInFlight,
	bool isAssetsLoaded)
{
	auto& swapDef = Backend::getSwapchainDef();
	framesInFlight = swapDef.imageCount;

	std::vector<std::unique_ptr<FrameCtx>> frameContexts;

	frameContexts.resize(framesInFlight);

	uint32_t graphicsIndex = Backend::getGraphicsQueue().familyIndex;
	uint32_t transferIndex = Backend::getTransferQueue().familyIndex;
	uint32_t computeIndex = Backend::getComputeQueue().familyIndex;

	size_t totalGPUStagingSize = 0;
	if (isAssetsLoaded) {
		totalGPUStagingSize =
			OPAQUE_INSTANCE_SIZE_BYTES +
			OPAQUE_INDIRECT_SIZE_BYTES +
			TRANSPARENT_INSTANCE_SIZE_BYTES +
			TRANSPARENT_INDIRECT_SIZE_BYTES +
			TRANSFORM_LIST_SIZE_BYTES;
	}

	fmt::print("Frames in flight:[{}]\n", framesInFlight);

	for (uint32_t i = 0; i < framesInFlight; ++i) {
		auto frame = std::make_unique<FrameCtx>();
		frame->frameIndex = i;

		frame->graphicsPool = CommandBuffer::createCommandPool(device, graphicsIndex);
		frame->transferPool = CommandBuffer::createCommandPool(device, transferIndex);
		frame->commandBuffer = CommandBuffer::createCommandBuffer(device, frame->graphicsPool);
		frame->set = DescriptorSetOverwatch::mainDescriptorManager.allocateDescriptor(device, frameLayout);
		frame->transferDeletion.semaphore = transferSync.semaphore;

		if (GPU_ACCELERATION_ENABLED) {
			frame->computePool = CommandBuffer::createCommandPool(device, computeIndex);
			frame->computeDeletion.semaphore = computeSync.semaphore;
		}

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

		if (isAssetsLoaded) {
			frame->addressTableStaging = BufferUtils::createBuffer(
				sizeof(GPUAddressTable),
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
				alloc);
			ASSERT(frame->addressTableStaging.info.pMappedData);

			frame->combinedGPUStaging = BufferUtils::createBuffer(
				totalGPUStagingSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
				alloc);
			ASSERT(frame->combinedGPUStaging.info.pMappedData);

			frame->opaqueInstanceBuffer = BufferUtils::createGPUAddressBuffer(
				AddressBufferType::OpaqueIntances, frame->addressTable, OPAQUE_INSTANCE_SIZE_BYTES, alloc);
			frame->persistentGPUBuffers.push_back(frame->opaqueInstanceBuffer);

			frame->opaqueIndirectCmdBuffer = BufferUtils::createGPUAddressBuffer(
				AddressBufferType::OpaqueIndirectDraws, frame->addressTable, OPAQUE_INDIRECT_SIZE_BYTES, alloc);
			frame->persistentGPUBuffers.push_back(frame->opaqueIndirectCmdBuffer);

			frame->transparentInstanceBuffer = BufferUtils::createGPUAddressBuffer(
				AddressBufferType::TransparentInstances, frame->addressTable, TRANSPARENT_INSTANCE_SIZE_BYTES, alloc);
			frame->persistentGPUBuffers.push_back(frame->transparentInstanceBuffer);

			frame->transparentIndirectCmdBuffer = BufferUtils::createGPUAddressBuffer(
				AddressBufferType::TransparentIndirectDraws, frame->addressTable, TRANSPARENT_INDIRECT_SIZE_BYTES, alloc);
			frame->persistentGPUBuffers.push_back(frame->transparentIndirectCmdBuffer);

			frame->transformsListBuffer = BufferUtils::createGPUAddressBuffer(
				AddressBufferType::Transforms, frame->addressTable, TRANSFORM_LIST_SIZE_BYTES, alloc);
			frame->persistentGPUBuffers.push_back(frame->transformsListBuffer);
		}

		frameContexts[i] = std::move(frame);
	}

	return frameContexts;
}

void FrameContext::cleanupFrameContexts(
	std::vector<std::unique_ptr<FrameCtx>>& frameContexts,
	const VkDevice device,
	const VmaAllocator alloc)
{
	for (auto& framePtr : frameContexts) {
		if (!framePtr) continue;

		auto& frame = *framePtr;

		frame.cpuDeletion.flush();

		for (auto& buf : frame.persistentGPUBuffers)
			BufferUtils::destroyAllocatedBuffer(buf, alloc);

		frame.transferCmds.clear();
		frame.transferDeletion.process(device);

		frame.computeCmds.clear();
		frame.computeDeletion.process(device);

		if (frame.graphicsPool != VK_NULL_HANDLE)
			vkDestroyCommandPool(device, frame.graphicsPool, nullptr);

		if (frame.transferPool != VK_NULL_HANDLE)
			vkDestroyCommandPool(device, frame.transferPool, nullptr);

		if (frame.computePool != VK_NULL_HANDLE)
			vkDestroyCommandPool(device, frame.computePool, nullptr);

		if (frame.stagingVisibleMeshIDsBuffer.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(frame.stagingVisibleMeshIDsBuffer, alloc);

		if (frame.stagingVisibleCountBuffer.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(frame.stagingVisibleCountBuffer, alloc);

		if (frame.gpuVisibleCountBuffer.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(frame.gpuVisibleCountBuffer, alloc);

		if (frame.gpuVisibleMeshIDsBuffer.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(frame.gpuVisibleMeshIDsBuffer, alloc);

		if (frame.combinedGPUStaging.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(frame.combinedGPUStaging, alloc);

		if (frame.addressTableStaging.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(frame.addressTableStaging, alloc);

		if (frame.addressTableBuffer.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(frame.addressTableBuffer, alloc);
	}

	frameContexts.clear();
}