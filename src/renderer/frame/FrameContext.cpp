#include "pch.h"

#include "FrameContext.h"
#include "renderer/backend/memory/Allocator.h"
#include "renderer/backend/memory/Budgets.h"
#include "renderer/backend/Device.h"
#include "renderer/backend/Descriptor.h"

void FrameContext::Init(
	uint32_t frameIndex,
	Device& device,
	DescriptorManager& descriptorsManager,
	Allocator& allocator)
{
	const auto& deviceCtx = device.GetContext();
	auto logicalDevice = deviceCtx.device;
	auto qIndices = deviceCtx.queueIndices;
	auto alloc  = allocator.GetVma();
	m_frameIndex = frameIndex;

	m_graphicsPool = device.CreateCommandPool(QueueType::Graphics);
	m_commandBuffer = device.CreateCommandBuffer(m_graphicsPool);
	m_frameSet = descriptorsManager.AllocateFrameDescriptorSet(logicalDevice);

	// GPU address table
	BufferDesc addressTableDesc {
		.size = GPU_ADDRESS_TABLE_SIZE_GPU_BYTES,
		.usage = static_cast<VkBufferUsageFlags>(BufferUsage::ADDRESS_TABLE),
		.debugName = "FrameAddressTable"
	};
	m_gpuAddressTable.emplace(allocator.AllocateBuffer(addressTableDesc));

	allocator.AllocateGPUBuffer(
		RD::Renderer_Buffer::VisibleInstances,
		m_gpuAddressTable.value(),
		MAX_INSTANCE_SIZE_GPU_BYTES);

	allocator.AllocateGPUBuffer(
		RD::Renderer_Buffer::IndirectDraws,
		m_gpuAddressTable.value(),
		MAX_INDIRECT_SIZE_GPU_BYTES);

	allocator.AllocateGPUBuffer(
		RD::Renderer_Buffer::VisibleLightCount,
		m_gpuAddressTable.value(),
		256u);

	allocator.AllocateGPUBuffer(
		RD::Renderer_Buffer::VisibleLightIDs,
		m_gpuAddressTable.value(),
		MAX_LIGHT_IDS_SIZE_GPU_BYTES);

	allocator.AllocateGPUBuffer(
		RD::Renderer_Buffer::DispatchIndirectArgs,
		m_gpuAddressTable.value(),
		256u);

	allocator.AllocateGPUBuffer(
		RD::Renderer_Buffer::Transforms,
		m_gpuAddressTable.value(),
		MAX_TRANSFORMS_SIZE_GPU_BYTES);

	allocator.AllocateGPUBuffer(
		RD::Renderer_Buffer::PrevTransforms,
		m_gpuAddressTable.value(),
		MAX_TRANSFORMS_SIZE_GPU_BYTES);

	allocator.AllocateGPUBuffer(
		RD::Renderer_Buffer::Lights,
		m_gpuAddressTable.value(),
		MAX_LIGHTS_SIZE_GPU_BYTES);


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

void FrameContext::CreateClusterBuffers(
	const uint32_t extentWidth,
	const uint32_t extentHeight,
	const VmaAllocator alloc)
{
	ClusterBufferSizes newClusterSizes;
	newClusterSizes = LightingSystem::computeClusterBufferSizes(
		extentWidth,
		extentHeight,
		m_clustered_UBO,
		alloc);
	m_bClusterWriteNeeded = true;

	for (auto& buf : m_clusterGPUBuffers) {
		if (buf.m_buffer != VK_NULL_HANDLE)
			BufferUtils::DestroyAllocatedBuffer(buf, alloc);
	}
	ClusterReset();

	m_clusterCounts_GPU = BufferUtils::CreateGPUAddressBuffer(
		BufferSlot::ClusterCounts,
		m_gpuAddressTable,
		newClusterSizes.clusterCountsBytes,
		alloc
	);
	m_clusterGPUBuffers.push_back(m_clusterCounts_GPU);

	m_clusterOffsets_GPU = BufferUtils::CreateGPUAddressBuffer(
		BufferSlot::ClusterOffsets,
		m_gpuAddressTable,
		newClusterSizes.clusterOffsetsBytes,
		alloc
	);
	m_clusterGPUBuffers.push_back(m_clusterOffsets_GPU);

	m_clusterCursors_GPU = BufferUtils::CreateGPUAddressBuffer(
		BufferSlot::ClusterCursors,
		m_gpuAddressTable,
		newClusterSizes.clusterCursorsBytes,
		alloc
	);
	m_clusterGPUBuffers.push_back(m_clusterCursors_GPU);

	m_clusterLightIDs_GPU = BufferUtils::CreateGPUAddressBuffer(
		BufferSlot::ClusterLightIDs,
		m_gpuAddressTable,
		newClusterSizes.clusterLightIDsBytes,
		alloc
	);
	m_clusterGPUBuffers.push_back(m_clusterLightIDs_GPU);

	m_clusterTileSliceRanges_GPU = BufferUtils::CreateGPUAddressBuffer(
		BufferSlot::ClusterTileSliceRanges,
		m_gpuAddressTable,
		newClusterSizes.clusterTileSliceRangesBytes,
		alloc
	);
	m_clusterGPUBuffers.push_back(m_clusterTileSliceRanges_GPU);

	m_clusterScanScratch_GPU = BufferUtils::CreateGPUAddressBuffer(
		BufferSlot::ClusterScanScratch,
		m_gpuAddressTable,
		newClusterSizes.clusterScanScratchBytes,
		alloc
	);
	m_clusterGPUBuffers.push_back(m_clusterScanScratch_GPU);
}

void FrameContext::CreateCMAA2Buffers(
	const uint32_t extentWidth,
	const uint32_t extentHeight,
	const VmaAllocator alloc)
{
	for (auto& buffer : m_cmaa2GPUBuffers)
	{
		BufferUtils::DestroyAllocatedBuffer(buffer, alloc);
	}
	Cmaa2Reset();

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

	cmaa2ControlBytes = BufferUtils::AlignUp(cmaa2ControlBytes, 256u);
	cmaa2ShapeCandidatesBytes = BufferUtils::AlignUp(cmaa2ShapeCandidatesBytes, 256u);
	cmaa2DeferredLocationsBytes = BufferUtils::AlignUp(cmaa2DeferredLocationsBytes, 256u);
	cmaa2DeferredItemsBytes = BufferUtils::AlignUp(cmaa2DeferredItemsBytes, 256u);
	cmaa2DeferredHeadsBytes = BufferUtils::AlignUp(cmaa2DeferredHeadsBytes, 256u);

	m_cmaa2Push.halfWidth = quadCountX;
	m_cmaa2Push.maxShapeCandidates = pixelCount;
	m_cmaa2Push.maxDeferredItems = deferredItemsCapacity;
	m_cmaa2Push.maxDeferredLocations = quadCount;

	m_cmaa2Control_GPU = BufferUtils::CreateGPUAddressBuffer(
		BufferSlot::Cmaa2Control,
		m_gpuAddressTable,
		cmaa2ControlBytes,
		alloc
	);
	m_cmaa2GPUBuffers.push_back(m_cmaa2Control_GPU);

	m_cmaa2ShapeCandidates_GPU = BufferUtils::CreateGPUAddressBuffer(
		BufferSlot::Cmaa2ShapeCandidates,
		m_gpuAddressTable,
		cmaa2ShapeCandidatesBytes,
		alloc
	);
	m_cmaa2GPUBuffers.push_back(m_cmaa2ShapeCandidates_GPU);

	m_cmaa2DeferredLocations_GPU = BufferUtils::CreateGPUAddressBuffer(
		BufferSlot::Cmaa2DeferredLocations,
		m_gpuAddressTable,
		cmaa2DeferredLocationsBytes,
		alloc
	);
	m_cmaa2GPUBuffers.push_back(m_cmaa2DeferredLocations_GPU);

	m_cmaa2DeferredItems_GPU = BufferUtils::CreateGPUAddressBuffer(
		BufferSlot::Cmaa2DeferredItems,
		m_gpuAddressTable,
		cmaa2DeferredItemsBytes,
		alloc
	);
	m_cmaa2GPUBuffers.push_back(m_cmaa2DeferredItems_GPU);

	m_cmaa2DeferredHeads_GPU = BufferUtils::CreateGPUAddressBuffer(
		BufferSlot::Cmaa2DeferredHeads,
		m_gpuAddressTable,
		cmaa2DeferredHeadsBytes,
		alloc
	);
	m_cmaa2GPUBuffers.push_back(m_cmaa2DeferredHeads_GPU);
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

void FrameContext::FreeStashedCmds()
{
	const VkDevice device = Backend::GetDevice();

	if (!m_transferCommandsToFree.empty()) {
		vkFreeCommandBuffers(
			device,
			m_transferPool,
			static_cast<uint32_t>(m_transferCommandsToFree.size()),
			m_transferCommandsToFree.data());
		m_transferCommandsToFree.clear();
	}
	if (!m_computeCommandsToFree.empty()) {
		vkFreeCommandBuffers(
			device,
			m_computePool,
			static_cast<uint32_t>(m_computeCommandsToFree.size()),
			m_computeCommandsToFree.data());
		m_computeCommandsToFree.clear();
	}
	if (!m_secondaryCommandsToFree.empty()) {
		vkFreeCommandBuffers(
			device,
			m_graphicsPool,
			static_cast<uint32_t>(m_secondaryCommandsToFree.size()),
			m_secondaryCommandsToFree.data());
		m_secondaryCommandsToFree.clear();
	}
}

void FrameContext::UpdateAddressTableIfDirty() {
	if (m_gpuAddressTable.IsTableDirty()) {
		m_descriptorWriter.WriteBuffer(
			ADDRESS_TABLE_BINDING,
			m_addressTable_GPU.m_buffer,
			sizeof(BindlessBufferTable),
			0,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			m_frameSet);

		m_gpuAddressTable.ClearTableDirty();
	}
}

void FrameContext::WriteFrameUniforms() {
	constexpr size_t offset = 0;

	if (m_visibleCount > 0) {
		m_descriptorWriter.WriteBuffer(
			FRAME_BINDING_CSM,
			m_directionalCSM_UBO.m_buffer,
			sizeof(DirectionalCSMInfo),
			offset,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			m_frameSet
		);
	}

	m_descriptorWriter.WriteBuffer(
		FRAME_BINDING_SCENE,
		m_sceneInfo_UBO.m_buffer,
		sizeof(SceneInfo),
		offset,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		m_frameSet
	);

	if (m_bClusterWriteNeeded) {
		m_descriptorWriter.WriteBuffer(
			FRAME_BINDING_CLUSTERED,
			m_clustered_UBO.m_buffer,
			sizeof(LightingSystem::ClusteredData),
			offset,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			m_frameSet
		);
		m_bClusterWriteNeeded = false;
	}
}

void FrameContext::UpdateFrameSet()
{
	m_descriptorWriter.UpdateSet(m_frameSet);
}

void FrameContext::Cleanup(const Allocator& allocator)
{
	m_cpuDeletionQueue.Flush();

	for (uint32_t i = 0; i < static_cast<uint32_t>(BufferSlot::Count); ++i)
	{
		BufferSlot bufferType = static_cast<BufferSlot>(i);
		m_gpuAddressTable.RemoveAddress(bufferType);
	}

	for (auto& buf : m_persistentGPUBuffers) {
		BufferUtils::DestroyAllocatedBuffer(buf, allocator);
	}

	BufferUtils::DestroyAllocatedBuffer(m_clustered_UBO, allocator);

	for (auto& buf : m_clusterGPUBuffers) {
		BufferUtils::DestroyAllocatedBuffer(buf, allocator);
	}
	ClusterReset();

	for (auto& buf : m_cmaa2GPUBuffers) {
		BufferUtils::DestroyAllocatedBuffer(buf, allocator);
	}
	Cmaa2Reset();

	const VkDevice device = Backend::GetDevice();

	if (m_graphicsTimestampPool != VK_NULL_HANDLE) {
		vkDestroyQueryPool(device, m_graphicsTimestampPool, nullptr);
	}

	FreeStashedCmds();

	m_transferCommands.clear();
	m_computeCommands.clear();
	m_secondaryCommands.clear();

	if (m_graphicsPool != VK_NULL_HANDLE)
		vkDestroyCommandPool(device, m_graphicsPool, nullptr);

	if (m_transferPool != VK_NULL_HANDLE)
		vkDestroyCommandPool(device, m_transferPool, nullptr);

	if (m_computePool != VK_NULL_HANDLE)
		vkDestroyCommandPool(device, m_computePool, nullptr);

	BufferUtils::DestroyAllocatedBuffer(m_gpuCopyStaging, allocator);

	BufferUtils::DestroyAllocatedBuffer(m_addressTable_GPU, allocator);
}
