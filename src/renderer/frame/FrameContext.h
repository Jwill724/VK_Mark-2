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

	BindlessBDATable& GetBindlessBDATable() { return m_gpuAddressTable; }

	void SetPendingAddrTableVersion(uint32_t version) { m_pendingAddressTableVersion = version; }
	uint32_t GetPendingAddrTableVersion() const noexcept { return m_pendingAddressTableVersion; }

	const std::vector<Instance>& GetVisibleInstances() const { return m_visibleInstances; }
	std::vector<Instance>& GetVisibleInstances() { return m_visibleInstances; }

	const std::vector<VkDrawIndexedIndirectCommand>& GetIndirectCmds() const { return m_indirectDraws; }
	std::vector<VkDrawIndexedIndirectCommand>& GetIndirectCmds() { return m_indirectDraws; }

	std::vector<VkCommandBuffer>& GetTransferCommands() { return m_transferCommands; }

	void CollectAndAppendCmds(std::vector<VkCommandBuffer>&& cmds, QueueType queue);
	void StashSubmitted(QueueType queue);
	void FreeStashedCmds(const DeviceContext& deviceCtx);

	void ClearDrawData()
	{
		m_visibleInstances.clear();
		m_indirectDraws.clear();
		m_opaqueDrawRange = {};
		m_opaqueInstanceRange = {};
		m_transparentDrawRange = {};
		m_transparentInstanceRange = {};
		m_flashlightShadowDrawRange = {};
		m_flashlightShadowInstanceRange = {};

		for (uint32_t i = 0; i < RD::MAX_SHADOW_CASCADES; ++i)
		{
			m_csmDrawRanges[i] = {};
			m_csmInstanceRanges[i] = {};
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
		return m_opaqueDrawRange.commandCount > 0 && m_opaqueInstanceRange.visibleCount > 0;
	}
	bool IsTransparentVisible() const noexcept
	{
		return m_transparentDrawRange.commandCount > 0 && m_transparentInstanceRange.visibleCount > 0;
	}

	bool IsThereVisibles() const noexcept
	{
		return IsOpaqueVisible() || IsTransparentVisible();
	}

	bool IsTemporalValid() const noexcept { return m_bIsTemporalValid; }

	const IndirectDrawRange& GetOpaqueDrawRange() const { return m_opaqueDrawRange; }
	IndirectDrawRange& GetOpaqueDrawRange() { return m_opaqueDrawRange; }

	const InstanceRange& GetOpaqueInstanceRange() const { return m_opaqueInstanceRange; }
	InstanceRange& GetOpaqueInstanceRange() { return m_opaqueInstanceRange; }

	const IndirectDrawRange& GetTransparentDrawRange() const { return m_transparentDrawRange; }
	IndirectDrawRange& GetTransparentDrawRange() { return m_transparentDrawRange; }

	const InstanceRange& GetTransparentInstanceRange() const { return m_transparentInstanceRange; }
	InstanceRange& GetTransparentInstanceRange() { return m_transparentInstanceRange; }

	const IndirectDrawRange& GetCSMDrawRange(uint32_t cascade) const;
	IndirectDrawRange& GetCSMDrawRange(uint32_t cascade);

	const InstanceRange& GetCSMInstanceRange(uint32_t cascade) const;
	InstanceRange& GetCSMInstanceRange(uint32_t cascade);

	const IndirectDrawRange& GetFlashlightDrawRange() const { return m_flashlightShadowDrawRange; }

	IndirectDrawRange& GetFlashlightDrawRange() { return m_flashlightShadowDrawRange; }

	const InstanceRange& GetFlashlightInstanceRange() const { return m_flashlightShadowInstanceRange; }

	InstanceRange& GetFlashlightInstanceRange() { return m_flashlightShadowInstanceRange; }

	void SetDrawData(DrawBuildOutput&& out)
	{
		m_visibleInstances = std::move(out.visibleInstances);
		m_indirectDraws = std::move(out.indirectDraws);

		m_opaqueInstanceRange = out.opaqueInstances;
		m_opaqueDrawRange = out.opaqueDraws;

		m_transparentInstanceRange = out.transparentInstances;
		m_transparentDrawRange = out.transparentDraws;

		for (uint32_t i = 0; i < RD::MAX_SHADOW_CASCADES; ++i)
		{
			m_csmDrawRanges[i] = out.csm[i].drawRange;
			m_csmInstanceRanges[i] = out.csm[i].instanceRange;
		}

		m_flashlightShadowDrawRange = out.flashlight.drawRange;
		m_flashlightShadowInstanceRange = out.flashlight.instanceRange;
	}

	void ValidateOpaque() const;
	void ValidateTransparent() const;
	void ValidateCSM(uint32_t cascade) const;
	void ValidateFlashlight() const;

	VkCommandPool  GetTransferPool()  const { return m_transferPool; }
	uint64_t&      GetTransferWaitValue()   { return transferWaitValue; }

	bool IsTransformsUploadNeeded()  const noexcept { return m_bTransformsBufferUploadNeeded; }
	bool IsLightsUploadNeeded()      const noexcept { return m_bLightsBufferUploadNeeded; }

	void ClearTransformsUploadFlag() noexcept { m_bTransformsBufferUploadNeeded = false; }
	void ClearLightsUploadFlag()     noexcept { m_bLightsBufferUploadNeeded = false; }

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
	std::array<IndirectDrawRange, RD::MAX_SHADOW_CASCADES> m_csmDrawRanges;
	std::array<InstanceRange, RD::MAX_SHADOW_CASCADES> m_csmInstanceRanges;

	// Shadow casters for flashlight
	IndirectDrawRange m_flashlightShadowDrawRange;
	InstanceRange m_flashlightShadowInstanceRange;

	IndirectDrawRange m_opaqueDrawRange;
	InstanceRange m_opaqueInstanceRange;
	IndirectDrawRange m_transparentDrawRange;
	InstanceRange m_transparentInstanceRange;

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
