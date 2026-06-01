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
	m_commandBuffer = device.CreateCommandBuffer(m_graphicsPool);
	m_frameSet = descriptorsManager.AllocateFrameDescriptorSet(logicalDevice);

	//////////////////////////////////////////////////////////////
	// THESE SSBOS REQUIRE STAGING COPIES

	// GPU address table
	m_gpuAddressTable.Init(allocator);

	////////////////////////////////////////////
	// gpu writable (not yet tho)
	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::VisibleInstances,
		MAX_INSTANCE_SIZE_GPU_BYTES,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::IndirectDraws,
		MAX_INDIRECT_SIZE_GPU_BYTES,
		allocator);
	///////////////////////////////////////////

	////////////////////////////////////////
	// Read only
	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::Transforms,
		MAX_TRANSFORMS_SIZE_GPU_BYTES,
		allocator);

	// Just needs temporal validation to copy Transforms (aka current transforms) to prevTransforms
	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::PrevTransforms,
		MAX_TRANSFORMS_SIZE_GPU_BYTES,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::Lights,
		MAX_LIGHTS_SIZE_GPU_BYTES,
		allocator);
	/////////////////////////////////////////

	///////////////////////////////////////////////////////////////


	// Every other ssbo can use cmdfill

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::VisibleLightCount,
		MIN_SSBO_ALIGNMENT_BYTES,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::VisibleLightIDs,
		MAX_LIGHT_IDS_SIZE_GPU_BYTES,
		allocator);

	m_gpuAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::DispatchIndirectArgs,
		MIN_SSBO_ALIGNMENT_BYTES,
		allocator);

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

void FrameContext::CollectAndAppendCmds(std::vector<VkCommandBuffer>&& cmds, QueueType queue)
{
	if (cmds.empty()) return;

	auto& dstCmds = (queue == QueueType::Transfer) ? m_transferCommands
		: (queue == QueueType::Compute) ? m_computeCommands
		: m_secondaryCommands;
	dstCmds.insert(dstCmds.end(),
		std::make_move_iterator(cmds.begin()),
		std::make_move_iterator(cmds.end()));
}

void FrameContext::StashSubmitted(QueueType queue)
{
	auto& srcCmds = (queue == QueueType::Transfer) ? m_transferCommands
		: (queue == QueueType::Compute) ? m_computeCommands
		: m_secondaryCommands;
	auto& dstCmds = (queue == QueueType::Transfer) ? m_transferCommandsToFree
		: (queue == QueueType::Compute) ? m_computeCommandsToFree
		: m_secondaryCommandsToFree;
	dstCmds.insert(dstCmds.end(), srcCmds.begin(), srcCmds.end());
	srcCmds.clear();
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
	if (!m_computeCommandsToFree.empty())
	{
		vkFreeCommandBuffers(
			deviceCtx.device,
			m_computePool,
			static_cast<uint32_t>(m_computeCommandsToFree.size()),
			m_computeCommandsToFree.data());
		m_computeCommandsToFree.clear();
	}
	if (!m_secondaryCommandsToFree.empty())
	{
		vkFreeCommandBuffers(
			deviceCtx.device,
			m_graphicsPool,
			static_cast<uint32_t>(m_secondaryCommandsToFree.size()),
			m_secondaryCommandsToFree.data());
		m_secondaryCommandsToFree.clear();
	}
}


void FrameContext::TickDescriptorWrites(DescriptorWriter& writer)
{
	// Most frequent writes

	writer.WriteBuffer(
		RD::FRAME_BINDING_SCENE,
		m_sceneInfo_UBO,
		m_frameSet);

	if (IsOpaqueVisible())
	{
		writer.WriteBuffer(
			RD::FRAME_BINDING_CSM,
			m_directionalCSM_UBO,
			m_frameSet);
	}

	// Less frequent writes

	if (m_gpuAddressTable.IsTableDirty())
	{
		writer.WriteBuffer(
			RD::ADDRESS_TABLE_BINDING,
			m_gpuAddressTable.GetTableBuffer(),
			m_frameSet);

		m_gpuAddressTable.ClearDirty();
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

const IndirectDrawRange& FrameContext::GetDirectionalCSMDrawRange(uint32_t cascade) const
{
	ASSERT(cascade < RD::MAX_SHADOW_CASCADES);
	return m_csmDrawRange[cascade];
};

void FrameContext::Cleanup(const DeviceContext& deviceCtx, Allocator& allocator)
{
	m_cpuDeletionQueue.Flush(); // Memory frees for SceneInfo and DirectionalCSM uniforms occur in here
	allocator.FreeBuffer(m_clustered_UBO);

	m_gpuAddressTable.Shutdown(allocator);

	if (m_graphicsTimestampPool != VK_NULL_HANDLE)
		vkDestroyQueryPool(deviceCtx.device, m_graphicsTimestampPool, nullptr);

	FreeStashedCmds(deviceCtx);

	m_transferCommands.clear();
	m_computeCommands.clear();
	m_secondaryCommands.clear();

	if (m_graphicsPool != VK_NULL_HANDLE)
		vkDestroyCommandPool(deviceCtx.device, m_graphicsPool, nullptr);

	if (m_transferPool != VK_NULL_HANDLE)
		vkDestroyCommandPool(deviceCtx.device, m_transferPool, nullptr);

	if (m_computePool != VK_NULL_HANDLE)
		vkDestroyCommandPool(deviceCtx.device, m_computePool, nullptr);
}
