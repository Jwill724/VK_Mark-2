#include "pch.h"

//#include "MeshLoader.h"
//#include "renderer/backend/Backend.h"
//#include "utils/BufferUtils.h"
//#include "renderer/gpu/CommandBuffer.h"
//#include "engine/Engine.h"
//#include "engine/JobSystem.h"
//
//void MeshLoader::uploadMeshes(
//	ThreadContext& threadCtx,
//	const std::vector<Vertex>& vertices,
//	const std::vector<uint32_t>& indices,
//	const MeshRegistry& meshes,
//	const VmaAllocator alloc,
//	const VkDevice device)
//{
//	const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
//	const size_t indexBufferSize = indices.size() * sizeof(uint32_t);
//	const size_t meshesSize = meshes.meshData.size() * sizeof(Mesh);
//	const size_t totalStagingSize = vertexBufferSize + indexBufferSize + meshesSize;
//
//	if (ENABLE_DEBUG_LOGS) {
//		JobSystem::Log(
//			threadCtx.threadID,
//			fmt::format(
//				"[MeshUpload] vertexBufferSize   = {} bytes ({} vertices)\n",
//				vertexBufferSize, vertices.size())
//		);
//		JobSystem::Log(
//			threadCtx.threadID,
//			fmt::format(
//				"[MeshUpload] indexBufferSize    = {} bytes ({} indices)\n",
//				indexBufferSize, indices.size())
//		);
//		JobSystem::Log(
//			threadCtx.threadID,
//			fmt::format(
//				"[MeshUpload] meshesSize         = {} bytes ({} meshes)\n",
//				meshesSize, meshes.meshData.size())
//		);
//		JobSystem::Log(
//			threadCtx.threadID,
//			fmt::format("[MeshUpload] totalStagingSize   = {} bytes\n", totalStagingSize)
//		);
//	}
//
//	auto& resources = Engine::GetState().getGPUResources();
//	auto& globalAddrTable = resources.GetAddressTable();
//
//	// Create large GPU buffers for vertex and index
//	AllocatedBuffer vtxBuffer = BufferUtils::CreateGPUAddressBuffer(
//		BufferSlot::Vertex,
//		globalAddrTable,
//		vertexBufferSize,
//		alloc
//	);
//	resources.AddGPUBufferToGlobalAddress(BufferSlot::Vertex, vtxBuffer);
//
//	AllocatedBuffer idxBuffer = BufferUtils::CreateGPUAddressBuffer(
//		BufferSlot::Index,
//		globalAddrTable,
//		indexBufferSize,
//		alloc
//	);
//	resources.AddGPUBufferToGlobalAddress(BufferSlot::Index, idxBuffer);
//
//	// Mesh buffer creation
//	AllocatedBuffer meshBuffer = BufferUtils::CreateGPUAddressBuffer(
//		BufferSlot::Mesh,
//		globalAddrTable,
//		meshesSize,
//		alloc
//	);
//	resources.AddGPUBufferToGlobalAddress(BufferSlot::Mesh, meshBuffer);
//
//	// Setup single staging buffer for transfer
//	AllocatedBuffer stagingBuffer = BufferUtils::CreateBuffer(
//		totalStagingSize,
//		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
//		VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
//		alloc
//	);
//	ASSERT(stagingBuffer.m_allocInfo.size >= totalStagingSize);
//
//	auto stgBuf = stagingBuffer.m_buffer;
//	auto stgAlloc = stagingBuffer.m_allocation;
//	resources.GetTempDQueue().PushFunction([stgBuf, stgAlloc, alloc]() mutable {
//		BufferUtils::DestroyBuffer(stgBuf, stgAlloc, alloc);
//	});
//
//	threadCtx.stagingMapped = stagingBuffer.m_allocInfo.pMappedData;
//	ASSERT(threadCtx.stagingMapped != nullptr);
//	uint8_t* const mappedStagingPtr = reinterpret_cast<uint8_t*>(threadCtx.stagingMapped);
//
//	// Compute offsets
//	const size_t vertexWriteOffset = 0;
//	const size_t indexWriteOffset = vertexWriteOffset + vertexBufferSize;
//	const size_t meshesWriteOffset = indexWriteOffset + indexBufferSize;
//
//	// Copy into staging
//	memcpy(mappedStagingPtr + vertexWriteOffset, vertices.data(), vertexBufferSize);
//	memcpy(mappedStagingPtr + indexWriteOffset, indices.data(), indexBufferSize);
//	memcpy(mappedStagingPtr + meshesWriteOffset, meshes.meshData.data(), meshesSize);
//
//	CommandBuffer::RecordDeferredCmd([&](VkCommandBuffer cmd) {
//		VkBufferCopy vtxCopy{
//			.srcOffset = vertexWriteOffset,
//			.dstOffset = 0,
//			.size = vertexBufferSize
//		};
//		vkCmdCopyBuffer(cmd, stagingBuffer.m_buffer, vtxBuffer.m_buffer, 1, &vtxCopy);
//
//		VkBufferCopy idxCopy{
//			.srcOffset = indexWriteOffset,
//			.dstOffset = 0,
//			.size = indexBufferSize
//		};
//		vkCmdCopyBuffer(cmd, stagingBuffer.m_buffer, idxBuffer.m_buffer, 1, &idxCopy);
//
//		VkBufferCopy meshCopy{
//			.srcOffset = meshesWriteOffset,
//			.dstOffset = 0,
//			.size = meshesSize
//		};
//		vkCmdCopyBuffer(cmd, stagingBuffer.m_buffer, meshBuffer.m_buffer, 1, &meshCopy);
//
//	}, threadCtx.cmdPool, QueueType::Transfer, device);
//
//	auto& tQueue = Backend::GetTransferQueue();
//	threadCtx.lastSubmittedFence = Engine::GetState().submitCommandBuffers(tQueue);
//	waitAndRecycleLastFence(threadCtx.lastSubmittedFence, tQueue, device);
//	vkResetCommandPool(device, threadCtx.cmdPool, 0);
//	threadCtx.cmdPool = VK_NULL_HANDLE;
//	threadCtx.stagingMapped = nullptr;
//}
