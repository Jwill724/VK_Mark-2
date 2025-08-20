#include "pch.h"

#include "MeshLoader.h"
#include "renderer/backend/Backend.h"
#include "utils/BufferUtils.h"
#include "renderer/gpu/CommandBuffer.h"
#include "engine/Engine.h"
#include "engine/JobSystem.h"

void MeshLoader::uploadMeshes(
	ThreadContext& threadCtx,
	const std::vector<Vertex>& vertices,
	const std::vector<uint32_t>& indices,
	const MeshRegistry& meshes,
	const VmaAllocator alloc,
	const VkDevice device)
{

	const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
	const size_t indexBufferSize = indices.size() * sizeof(uint32_t);
	const size_t meshesSize = meshes.meshData.size() * sizeof(GPUMeshData);
	const size_t totalStagingSize = vertexBufferSize + indexBufferSize + meshesSize;

	JobSystem::log(
		threadCtx.threadID,
		fmt::format(
			"[MeshUpload] vertexBufferSize   = {} bytes ({} vertices)\n",
			vertexBufferSize, vertices.size())
	);
	JobSystem::log(
		threadCtx.threadID,
		fmt::format(
			"[MeshUpload] indexBufferSize    = {} bytes ({} indices)\n",
			indexBufferSize, indices.size())
	);
	JobSystem::log(
		threadCtx.threadID,
		fmt::format(
			"[MeshUpload] meshesSize         = {} bytes ({} meshes)\n",
			meshesSize, meshes.meshData.size())
	);
	JobSystem::log(
		threadCtx.threadID,
		fmt::format("[MeshUpload] totalStagingSize   = {} bytes\n", totalStagingSize)
	);

	auto& resources = Engine::getState().getGPUResources();

	// Create large GPU buffers for vertex and index
	AllocatedBuffer vtxBuffer = BufferUtils::createGPUAddressBuffer(
		AddressBufferType::Vertex,
		resources.getAddressTable(),
		vertexBufferSize,
		alloc
	);
	resources.addGPUBufferToGlobalAddress(AddressBufferType::Vertex, vtxBuffer);

	AllocatedBuffer idxBuffer = BufferUtils::createGPUAddressBuffer(
		AddressBufferType::Index,
		resources.getAddressTable(),
		indexBufferSize,
		alloc
	);
	resources.addGPUBufferToGlobalAddress(AddressBufferType::Index, idxBuffer);

	// Mesh buffer creation
	AllocatedBuffer meshBuffer = BufferUtils::createGPUAddressBuffer(
		AddressBufferType::Mesh,
		resources.getAddressTable(),
		meshesSize,
		alloc
	);
	resources.addGPUBufferToGlobalAddress(AddressBufferType::Mesh, meshBuffer);

	// Setup single staging buffer for transfer
	AllocatedBuffer stagingBuffer = BufferUtils::createBuffer(
		totalStagingSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
		alloc
	);
	ASSERT(stagingBuffer.info.size >= totalStagingSize);

	auto stgBuf = stagingBuffer.buffer;
	auto stgAlloc = stagingBuffer.allocation;
	resources.getTempDeletionQueue().push_function([stgBuf, stgAlloc, alloc]() mutable {
		BufferUtils::destroyBuffer(stgBuf, stgAlloc, alloc);
	});

	threadCtx.stagingMapped = stagingBuffer.info.pMappedData;
	ASSERT(threadCtx.stagingMapped != nullptr);
	uint8_t* stagingData = reinterpret_cast<uint8_t*>(threadCtx.stagingMapped);

	// Compute offsets
	VkDeviceSize vertexWriteOffset = 0;
	VkDeviceSize indexWriteOffset = vertexWriteOffset + vertexBufferSize;
	VkDeviceSize meshesWriteOffset = indexWriteOffset + indexBufferSize;

	JobSystem::log(
		threadCtx.threadID,
		fmt::format("[MeshUpload] vertexWriteOffset     = {}\n", vertexWriteOffset));
	JobSystem::log(
		threadCtx.threadID,
		fmt::format("[MeshUpload] indexWriteOffset      = {}\n", indexWriteOffset));
	JobSystem::log(
		threadCtx.threadID,
		fmt::format("[MeshUpload] meshesWriteOffset     = {}\n", meshesWriteOffset));

	// Copy into staging
	memcpy(stagingData + vertexWriteOffset, vertices.data(), vertexBufferSize);
	memcpy(stagingData + indexWriteOffset, indices.data(), indexBufferSize);
	memcpy(stagingData + meshesWriteOffset, meshes.meshData.data(), meshesSize);

	CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {
		VkBufferCopy vtxCopy{
			.srcOffset = vertexWriteOffset,
			.dstOffset = 0,
			.size = vertexBufferSize
		};
		vkCmdCopyBuffer(cmd, stagingBuffer.buffer, vtxBuffer.buffer, 1, &vtxCopy);

		VkBufferCopy idxCopy{
			.srcOffset = indexWriteOffset,
			.dstOffset = 0,
			.size = indexBufferSize
		};
		vkCmdCopyBuffer(cmd, stagingBuffer.buffer, idxBuffer.buffer, 1, &idxCopy);

		VkBufferCopy meshCopy{
			.srcOffset = meshesWriteOffset,
			.dstOffset = 0,
			.size = meshesSize
		};
		vkCmdCopyBuffer(cmd, stagingBuffer.buffer, meshBuffer.buffer, 1, &meshCopy);

	}, threadCtx.cmdPool, QueueType::Transfer, device);

	resources.updateAddressTableMapped(threadCtx.cmdPool);

	auto& tQueue = Backend::getTransferQueue();
	threadCtx.lastSubmittedFence = Engine::getState().submitCommandBuffers(tQueue);
	waitAndRecycleLastFence(threadCtx.lastSubmittedFence, tQueue, device);
	vkResetCommandPool(device, threadCtx.cmdPool, 0);
	threadCtx.cmdPool = VK_NULL_HANDLE;
	threadCtx.stagingMapped = nullptr;
}