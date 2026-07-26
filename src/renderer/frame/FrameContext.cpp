#include "pch.h"

#include "FrameContext.h"
#include "../backend/memory/ResourceAllocator.h"
#include "../backend/memory/Budgets.h"
#include "../backend/Device.h"
#include "../backend/DescriptorManager.h"
#include "../backend/DescriptorWriter.h"
#include "FrameResources.h"

void FrameContext::Init(
	uint32_t frameIndex,
	uint32_t threadSlotCount,
	Extents2D drawExtent,
	Device& device,
	DescriptorManager& descriptorsManager,
	Allocator& allocator)
{
	const auto& deviceCtx = device.GetContext();
	auto logicalDevice = deviceCtx.device;
	auto qIndices = deviceCtx.queueIndices;
	m_frameIndex = frameIndex;

	m_cachedDrawExtent = drawExtent;

	m_graphicsPool = device.CreateCommandPool(QueueType::Graphics);
	m_transferPool = device.CreateCommandPool(QueueType::Transfer);
	m_computePool  = device.CreateCommandPool(QueueType::Compute);

	device.CreateCommandBuffers(m_graphicsPool, m_graphicsPrimaries.data(), RD::MAX_GRAPHICS_PRIMARIES);
	m_asyncComputeCmd = device.CreateCommandBuffer(m_computePool);

	const uint32_t gfxFamily     = device.GetGraphicsQueue().GetFamilyIndex();
	const uint32_t computeFamily = device.GetComputeQueue().GetFamilyIndex();

	if (gfxFamily != computeFamily)
	{
		m_secondaryArena.Init(
			device.GetContext().device,
			threadSlotCount,
			computeFamily);
	}

	m_frameSet = descriptorsManager.AllocateFrameDescriptorSet(logicalDevice);

	// GPU address table
	m_gpuAddressTable.Init(allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::Transforms,
		GPU_BYTES_TRANSFORMS,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::PrevTransforms,
		GPU_BYTES_TRANSFORMS,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::Lights,
		GPU_BYTES_LIGHTS,
		allocator);

	// Cull outputs
	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::InstanceVisibility,
		GPU_BYTES_INSTANCE_VISIBILITY,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::VisibleCount,
		sizeof(uint32_t),
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::VisibleInstances,
		GPU_BYTES_VISIBLE_INSTANCES,
		allocator);

	// Draw build intermediates
	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::InstanceStreams,
		GPU_BYTES_INSTANCE_STREAMS,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::InstanceCursors,
		GPU_BYTES_INSTANCE_STREAMS,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::DrawInstanceIDs,
		GPU_BYTES_DRAW_INSTANCE_IDS,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::IndirectDraws,
		GPU_BYTES_INDIRECT_DRAWS,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::IndirectDrawCounts,
		GPU_BYTES_INDIRECT_DRAW_COUNTS,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::DrawBins,
		GPU_BYTES_DRAW_BINS,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::DrawBinCounters,
		GPU_BYTES_DRAW_BIN_COUNTERS,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::ShadowCullData,
		GPU_BYTES_SHADOW_CULL_DATA,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::DispatchIndirectArgs,
		GPU_BYTES_DISPATCH_INDIRECT_ARGS,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::DrawStats,
		sizeof(GPUStats),
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::VisibleLightCount,
		sizeof(uint32_t),
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::VisibleLightIDs,
		GPU_BYTES_VISIBLE_LIGHT_IDS,
		allocator);

	// Readback stats buffer
	m_statsReadback = allocator.AllocateBuffer({
		sizeof(GPUStats),
		Vulkan_BufferUsage::READ_BACK,
		HeapType::Readback
	});

	vmaMapMemory(allocator.GetVma(),
		m_statsReadback.m_allocation,
		reinterpret_cast<void**>(const_cast<GPUStats**>(&m_statsMapped)));

	// Light cluster buffers
	ClusterBufferSizes clusterBufSizes;
	clusterBufSizes.UpdateClusterBufferSizes(m_cachedDrawExtent.Width(), m_cachedDrawExtent.Height());
	CreateClusterBuffers(clusterBufSizes, allocator);

	// Cmaa2 buffers
	Cmaa2BufferSizes cmaa2BufSizes;
	cmaa2BufSizes.UpdateCmaa2BufferSizes(m_cachedDrawExtent.Width(), m_cachedDrawExtent.Height());
	CreateCMAA2Buffers(cmaa2BufSizes, allocator);

	VkQueryPoolCreateInfo queryPoolInfo{};
	queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
	queryPoolInfo.queryCount = TIMESTAMP_QUERY_COUNT;

	VK_CHECK(vkCreateQueryPool(
		logicalDevice,
		&queryPoolInfo,
		nullptr,
		&m_graphicsTimestampPool
	));

	VK_CHECK(vkCreateQueryPool(
		logicalDevice,
		&queryPoolInfo,
		nullptr,
		&m_computeTimestampPool
	));

	for (uint32_t passIndex = 0; passIndex < TIMESTAMP_PASS_COUNT; ++passIndex)
	{
		m_passTimestampRanges[passIndex].beginQuery = passIndex * 2;
		m_passTimestampRanges[passIndex].endQuery = passIndex * 2 + 1;
	}

	m_timestampResults.fill(0);
}

void FrameContext::ClusterReset(Allocator& allocator)
{
	for (size_t i = static_cast<size_t>(RD::Renderer_Buffer::ClusterCounts);
		i <= static_cast<size_t>(RD::Renderer_Buffer::ClusterScanScratch);
		i++)
	{
		m_gpuAddressTable.ClearGPUAddressBuffer(static_cast<RD::Renderer_Buffer>(i), allocator);
	}
}

void FrameContext::CreateDebugBuffers(Allocator& allocator)
{
	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::DebugCounts,
		GPU_BYTES_DEBUG_COUNTERS,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::DebugItems,
		GPU_BYTES_DEBUG_ITEMS,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::DebugVertex,
		GPU_BYTES_DEBUG_VERTEX,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::DebugDraw,
		RD::INDIRECT_CMD_SIZE,
		allocator);
}

void FrameContext::DestroyDebugBuffers(Allocator& allocator)
{
	for (size_t i = static_cast<size_t>(RD::Renderer_Buffer::DebugCounts);
		i <= static_cast<size_t>(RD::Renderer_Buffer::DebugDraw);
		i++)
	{
		m_gpuAddressTable.ClearGPUAddressBuffer(static_cast<RD::Renderer_Buffer>(i), allocator);
	}
}

void FrameContext::CreateClusterBuffers(
	const ClusterBufferSizes& clusterBufSizes,
	Allocator& allocator)
{
	ClusterReset(allocator);

	// Uniform buffer updates
	allocator.FreeBuffer(m_clustered_UBO);
	m_clusterData.tileCountX = clusterBufSizes.tileCountX;
	m_clusterData.tileCountY = clusterBufSizes.tileCountY;
	m_clusterData.clusterCount = clusterBufSizes.clusterCount;
	m_clustered_UBO = allocator.AllocateUniform(m_clusterData);
	m_bClusterUniformWriteNeeded = true; // Descriptor update needed

	// Ssbo updates

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::ClusterCounts,
		clusterBufSizes.clusterCountsBytes,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::ClusterOffsets,
		clusterBufSizes.clusterOffsetsBytes,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::ClusterCursors,
		clusterBufSizes.clusterCursorsBytes,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::ClusterLightIDs,
		clusterBufSizes.clusterLightIDsBytes,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::ClusterTileSliceRanges,
		clusterBufSizes.clusterTileSliceRangesBytes,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::ClusterScanScratch,
		clusterBufSizes.clusterScanScratchBytes,
		allocator);
}

void FrameContext::Cmaa2Reset(Allocator& allocator)
{
	for (size_t i = static_cast<size_t>(RD::Renderer_Buffer::Cmaa2Control);
		i <= static_cast<size_t>(RD::Renderer_Buffer::Cmaa2DeferredHeads);
		i++)
	{
		m_gpuAddressTable.ClearGPUAddressBuffer(static_cast<RD::Renderer_Buffer>(i), allocator);
	}
}

void FrameContext::CreateCMAA2Buffers(
	const Cmaa2BufferSizes& cmaa2BufSizes,
	Allocator& allocator)
{
	Cmaa2Reset(allocator);

	// Push constant updates
	m_cmaa2Push.halfWidth = cmaa2BufSizes.quadCountX;
	m_cmaa2Push.maxShapeCandidates = cmaa2BufSizes.pixelCount;
	m_cmaa2Push.maxDeferredItems = cmaa2BufSizes.deferredItemsCapacity;
	m_cmaa2Push.maxDeferredLocations = cmaa2BufSizes.quadCount;

	// Ssbo updates

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::Cmaa2Control,
		cmaa2BufSizes.controlBytes,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::Cmaa2ShapeCandidates,
		cmaa2BufSizes.shapeCandidatesBytes,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::Cmaa2DeferredLocations,
		cmaa2BufSizes.deferredLocationsBytes,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::Cmaa2DeferredItems,
		cmaa2BufSizes.deferredItemsBytes,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::Cmaa2DeferredHeads,
		cmaa2BufSizes.deferredHeadsBytes,
		allocator);
}

void FrameContext::CollectTransferCmds(std::vector<VkCommandBuffer>&& cmds, QueueType queue)
{
	if (cmds.empty() || queue != QueueType::Transfer) return;

	m_transferCommands.insert(m_transferCommands.end(),
		std::make_move_iterator(cmds.begin()),
		std::make_move_iterator(cmds.end()));
}

void FrameContext::StashTransferCmds()
{
	m_transferCommandsToFree.insert(m_transferCommandsToFree.end(), m_transferCommands.begin(), m_transferCommands.end());
	m_transferCommands.clear();
}

void FrameContext::FreeStashedCmds(const DeviceContext& deviceCtx)
{
	if (!m_transferCommandsToFree.empty())
	{
		vkFreeCommandBuffers(
			deviceCtx.device,
			m_transferPool,
			static_cast<uint32_t>(m_transferCommandsToFree.size()),
			m_transferCommandsToFree.data());
		m_transferCommandsToFree.clear();
	}
}


void FrameContext::TickDescriptorWrites(DescriptorWriter& writer)
{
	// Most frequent writes
	writer.WriteBuffer(
		RD::FRAME_BINDING_SCENE,
		m_sceneInfo_UBO,
		m_frameSet);

	writer.WriteBuffer(
		RD::FRAME_BINDING_CSM,
		m_directionalCSM_UBO,
		m_frameSet);

	// Less frequent writes

	if (m_gpuAddressTable.IsTableDirty())
	{
		writer.WriteBuffer(
			RD::ADDRESS_TABLE_BINDING,
			m_gpuAddressTable.GetTableBuffer(),
			m_frameSet);
	}

	if (m_bClusterUniformWriteNeeded)
	{
		writer.WriteBuffer(
			RD::FRAME_BINDING_CLUSTERED,
			m_clustered_UBO,
			m_frameSet);
		m_bClusterUniformWriteNeeded = false;
	}
}

void FrameContext::AssignSceneUniform(AllocatedBuffer buffer, const Allocator& allocator)
{
	m_sceneInfo_UBO = std::move(buffer);
	m_cpuDeletionQueue.PushFunction([buffer = m_sceneInfo_UBO, &allocator]() mutable {
		allocator.FreeBuffer(buffer);
	});
}
void FrameContext::AssignCSMUniform(AllocatedBuffer buffer, const Allocator& allocator)
{
	m_directionalCSM_UBO = std::move(buffer);
	m_cpuDeletionQueue.PushFunction([buffer = m_directionalCSM_UBO, &allocator]() mutable {
		allocator.FreeBuffer(buffer);
	});
}

void FrameContext::Cleanup(const DeviceContext& deviceCtx, Allocator& allocator)
{
	m_cpuDeletionQueue.Flush(); // Memory frees for SceneInfo and DirectionalCSM uniforms occur in here
	allocator.FreeBuffer(m_clustered_UBO);

	if (m_statsMapped)
	{
		vmaUnmapMemory(allocator.GetVma(), m_statsReadback.m_allocation);
		m_statsMapped = nullptr;
	}
	allocator.FreeBuffer(m_statsReadback);

	m_gpuAddressTable.Shutdown(allocator);

	if (m_graphicsTimestampPool != VK_NULL_HANDLE)
		vkDestroyQueryPool(deviceCtx.device, m_graphicsTimestampPool, nullptr);

	if (m_computeTimestampPool != VK_NULL_HANDLE)
		vkDestroyQueryPool(deviceCtx.device, m_computeTimestampPool, nullptr);

	FreeStashedCmds(deviceCtx);

	m_secondaryArena.Cleanup();

	m_transferCommands.clear();

	if (m_graphicsPool != VK_NULL_HANDLE)
		vkDestroyCommandPool(deviceCtx.device, m_graphicsPool, nullptr);

	if (m_transferPool != VK_NULL_HANDLE)
		vkDestroyCommandPool(deviceCtx.device, m_transferPool, nullptr);

	if (m_computePool != VK_NULL_HANDLE)
		vkDestroyCommandPool(deviceCtx.device, m_computePool, nullptr);
}
