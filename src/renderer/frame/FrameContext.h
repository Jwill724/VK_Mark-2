#pragma once

#include "../backend/VulkanForward.h"
#include "../RendererDefinitions.h"
#include "../backend/memory/BindlessBDATable.h"
#include "EngineTypes.h"
#include "FrameResources.h"

namespace RD = RendererDefinitions;

class Device;
struct DeviceContext;
class DescriptorManager;
class DescriptorWriter;
class Allocator;
struct Cmaa2BufferSizes;
struct ClusterBufferSizes;

enum class QueueType;

class Renderer;
class Profiler;

class FrameContext
{
	friend class Renderer;
	friend class Profiler;
public:
	void Init(
		uint32_t frameIndex,
		Extents2D drawExtent,
		Device& device,
		DescriptorManager& descriptorsManager,
		Allocator& allocator);
	void Cleanup(const DeviceContext& deviceCtx, Allocator& allocator);

	void CollectAndAppendCmds(std::vector<VkCommandBuffer>&& cmds, QueueType queue);
	void StashSubmitted(QueueType queue);
	void FreeStashedCmds(const DeviceContext& deviceCtx);

	void ClearDrawData()
	{
		m_visibleInstances.clear();
		m_indirectDraws.clear();
		m_opaqueDrawRange = {};
		m_transparentDrawRange = {};
		m_visibleShadowCasters.clear();
		m_flashlightShadowDrawRange = {};
		for (uint32_t i = 0; i < RD::MAX_SHADOW_CASCADES; ++i)
		{
			m_csmDrawRange[i] = {};
			//m_shadowDrawRanges[i] = {};
		}
	}

	void ClusterReset(Allocator& allocator);
	void CreateClusterBuffers(
		const ClusterBufferSizes& clusterBufSizes,
		Allocator& allocator);

	void CreateCMAA2Buffers(
		const Cmaa2BufferSizes& cmaa2BufSizes,
		Allocator& allocator);

	void Cmaa2Reset(Allocator& allocator);

	const AllocatedBuffer& GetGPUBuffer(RD::Renderer_Buffer buffer) const { return m_gpuAddressTable.GetGPUBuffer(buffer); }

	const RD::PassTimestampRange& GetTimestampRange(RD::Renderer_Pass pass) const
	{
		return m_passTimestampRanges[static_cast<uint32_t>(pass)];
	}

	bool IsOpaqueVisible() const noexcept
	{
		return m_opaqueDrawRange.visibleCount > 0 && m_opaqueDrawRange.commandCount > 0;
	}
	bool IsTransparentVisible() const noexcept
	{
		return m_transparentDrawRange.visibleCount > 0 && m_transparentDrawRange.commandCount > 0;
	}

	bool IsTemporalValid() const noexcept { return m_bIsTemporalValid; }

	const IndirectDrawRange& GetOpaqueDrawRange() const { return m_opaqueDrawRange; }
	const IndirectDrawRange& GetTransparentDrawRange() const { return m_transparentDrawRange; }
	const IndirectDrawRange& GetDirectionalCSMDrawRange(uint32_t cascade) const;
	const IndirectDrawRange& GetFlashlightShadowDrawRange() const { return m_flashlightShadowDrawRange; }

	bool DoesCachedExtentNeedUpdate(uint32_t width, uint32_t height) noexcept
	{
		if (m_cachedDrawExtent.Width() != width || m_cachedDrawExtent.Height() != height)
		{
			m_cachedDrawExtent.Width() = width;
			m_cachedDrawExtent.Height() = height;
			return true;
		}
		return false;
	}

	const Extents2D& GetCachedExtent() const { return m_cachedDrawExtent; }

	void AssignSceneUniform(AllocatedBuffer buffer, const Allocator& allocator);
	void AssignCSMUniform(AllocatedBuffer buffer, const Allocator& allocator);

	void IsFlashlightStateVersionOld(uint32_t version) noexcept
	{
		if (m_uploadedFlashlightVersion != version)
		{
			m_bLightsBufferUploadNeeded = true;
			m_uploadedFlashlightVersion = version;
		}
	}

	// Requires update when count hasn't changed besides dynamic and static states
	void EvaluatePossibleLightUpdateStatus() noexcept
	{
		if (m_bRecentDynamicLightsTransform && !m_bLightsBufferUploadNeeded)
		{
			m_bLightsBufferUploadNeeded = true;
			m_bRecentDynamicLightsTransform = false;
		}
	}
	void MarkDynamicLightTransforms() { m_bRecentDynamicLightsTransform = true; }


	void EvaluateLightListSizeChanges(size_t listSize)
	{
		auto lightListSize = static_cast<uint32_t>(listSize);
		if (m_recentLightListCount != lightListSize)
		{
			m_recentLightListCount = lightListSize;
			m_bLightsBufferUploadNeeded = true;
		}
	}

	void IsFirstLightsUpload(bool lightsExist)
	{
		if (lightsExist && !m_bLightsInitialized)
		{
			m_bLightsInitialized = true;
		}
	}

	void MarkLightUpload()
	{
		m_bLightsBufferUploadNeeded = true;
	}

	void EvaluateTransformsStatus(bool uploadNeeded)
	{
		if (uploadNeeded || !m_bTransformsInitialized)
		{
			m_bTransformsBufferUploadNeeded = true;
			m_bTransformsInitialized = true;
		}
	}

	void SetVisibilityResult(VisibilitySyncResult result) { m_visibilitySyncResult = std::move(result); }

	CMAA2Push& GetCMAA2Push() { return m_cmaa2Push; }

	const LightClustersData& GetClusterData() const { return m_clusterData; }

	const AllocatedBuffer& GetOBBLineDebugBuffer() const { return m_obbLineDebug_GPU; }
	const std::vector<uint32_t>& GetOBBDrawOffsets() const { return m_obbDrawOffsets; }

	const VisibilitySyncResult& GetVisibilitySyncResult() const { return m_visibilitySyncResult; }

private:
	uint32_t m_frameIndex = 0u;

	uint32_t m_swapchainImageIndex = 0u;

	VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE; // primary graphics command
	VkCommandPool m_graphicsPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> m_secondaryCommands;

	uint64_t transferWaitValue = UINT64_MAX;
	VkCommandPool m_transferPool;
	std::vector<VkCommandBuffer> m_transferCommands;

	// === async compute ===
	std::vector<VkCommandBuffer> m_computeCommands;
	VkCommandPool m_computePool = VK_NULL_HANDLE;
	uint64_t m_computeWaitValue = UINT64_MAX;

	std::vector<VkCommandBuffer> m_transferCommandsToFree;
	std::vector<VkCommandBuffer> m_computeCommandsToFree;
	std::vector<VkCommandBuffer> m_secondaryCommandsToFree;

	// Flattened instance + command buffers
	std::vector<Instance> m_visibleInstances;
	std::vector<VkDrawIndexedIndirectCommand> m_indirectDraws;

	uint32_t m_recentLightListCount = 0u;
	uint32_t m_uploadedFlashlightVersion = 0u;

	VkQueryPool m_graphicsTimestampPool = VK_NULL_HANDLE;
	bool m_bHasTimestampResultsPending = false;
	std::array<RD::PassTimestampRange, TIMESTAMP_PASS_COUNT> m_passTimestampRanges{};
	std::array<uint64_t, PASS_TIMESTAMP_QUERY_COUNT> m_timestampResults{};
	std::array<bool, TIMESTAMP_PASS_COUNT> m_timestampPassUsed{};

	// Visible instances from directional light perspective
	std::vector<Instance> m_visibleShadowCasters;
	std::array<IndirectDrawRange, RD::MAX_SHADOW_CASCADES> m_csmDrawRange;

	// Shadow casters for flashlight
	IndirectDrawRange m_flashlightShadowDrawRange;

	IndirectDrawRange m_opaqueDrawRange;
	IndirectDrawRange m_transparentDrawRange;

	VisibilitySyncResult m_visibilitySyncResult;

	Extents2D m_cachedDrawExtent;

	// Takes renderer descriptor writer
	void TickDescriptorWrites(DescriptorWriter& writer);

	void SetTemporalResult(bool result) { m_bIsTemporalValid = result; }
	bool m_bIsTemporalValid = false;

	CMAA2Push m_cmaa2Push;

	LightClustersData m_clusterData;

	// Culling data
	//VisibilityPush m_visibilityPush{};

	bool m_bTransformsInitialized = false;
	bool m_bTransformsBufferUploadNeeded = false;

	bool m_bLightsInitialized = false;
	bool m_bLightsBufferUploadNeeded = false;
	bool m_bRecentDynamicLightsTransform = false;

	// frame owned gpu buffers
	BindlessBDATable m_gpuAddressTable;
	uint32_t m_pendingAddressTableVersion = 0u;

	// Vertex buffer, address is pulled via push constant
	AllocatedBuffer m_obbLineDebug_GPU;
	std::vector<uint32_t> m_obbDrawOffsets;

	AllocatedBuffer m_directionalCSM_UBO;

	AllocatedBuffer m_sceneInfo_UBO;

	bool m_bClusterUniformWriteNeeded = false;
	AllocatedBuffer m_clustered_UBO;

	VkDescriptorSet m_frameSet = VK_NULL_HANDLE;

	DeletionQueue m_cpuDeletionQueue;
};
