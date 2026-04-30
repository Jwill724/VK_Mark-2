#pragma once

#include "renderer/RendererDefinitions.h"
#include "renderer/backend/memory/AllocatedBuffer.h"
#include "renderer/backend/DescriptorWriter.h"
#include "ResourceTypes.h"
#include "EngineTypes.h"

namespace RD = RendererDefinitions;

inline constexpr uint32_t TIMESTAMP_QUERIES_PER_PASS = 2;
inline constexpr uint32_t TIMESTAMP_PASS_COUNT = static_cast<uint32_t>(RD::Renderer_Pass::None);
inline constexpr uint32_t PASS_TIMESTAMP_QUERY_COUNT = TIMESTAMP_PASS_COUNT * TIMESTAMP_QUERIES_PER_PASS;

inline constexpr uint32_t FRAME_BEGIN_QUERY = PASS_TIMESTAMP_QUERY_COUNT + 0;
inline constexpr uint32_t FRAME_END_QUERY = PASS_TIMESTAMP_QUERY_COUNT + 1;
inline constexpr uint32_t TIMESTAMP_QUERY_COUNT = PASS_TIMESTAMP_QUERY_COUNT + 2;

class Device;
class DescriptorManager;
class Allocator;

class FrameContext
{
	friend class Renderer;
public:
	void Init(
		uint32_t frameIndex,
		VkExtent2D drawExtent,
		Device& device,
		DescriptorManager& descriptorsManager,
		Allocator& allocator);
	void Cleanup(const Allocator& allocator);

	void CollectAndAppendCmds(std::vector<VkCommandBuffer>&& cmds, QueueType queue);
	void StashSubmitted(QueueType queue);
	void FreeStashedCmds();

	void WriteFrameUniforms();

	void UpdateFrameSet();

	void ClearDrawData()
	{
		m_visibleInstances.clear();
		m_indirectDraws.clear();
		m_visibleCount = 0;
		m_opaqueDrawRange = {};
		m_transparentDrawRange = {};
		m_visibleShadowCasters.clear();
		m_flashlightShadowCasterDrawRange = {};
		for (uint32_t i = 0; i < RD::MAX_SHADOW_CASCADES; ++i) {
			m_shadowCasterDrawRanges[i] = {};
			//m_shadowDrawRanges[i] = {};
		}
	}

	void ClusterReset()
	{
		for (size_t i = static_cast<size_t>(RD::Renderer_Buffer::ClusterCounts);
			i <= static_cast<size_t>(RD::Renderer_Buffer::ClusterTileSliceRanges);
			i++)
		{
			m_gpuAddressTable.ResetGPUAddressBuffer(static_cast<RD::Renderer_Buffer>(i));
		}
	}
	void CreateClusterBuffers(const ClusterBufferSizes& clusterBufSizes, Allocator& allocator);

	void CreateCMAA2Buffers(
		const uint32_t extentWidth,
		const uint32_t extentHeight,
		Allocator& allocator);

	void Cmaa2Reset()
	{
		for (size_t i = static_cast<size_t>(RD::Renderer_Buffer::Cmaa2Control);
			i <= static_cast<size_t>(RD::Renderer_Buffer::Cmaa2DeferredHeads);
			i++)
		{
			m_gpuAddressTable.ResetGPUAddressBuffer(static_cast<RD::Renderer_Buffer>(i));
		}
	}

	const PassTimestampRange& GetTimestampRange(RD::Renderer_Pass pass) const {
		return m_passTimestampRanges[static_cast<uint32_t>(pass)];
	}

	void UpdateAddressTableIfDirty();


private:
	uint32_t m_frameIndex = 0u;

	VkResult m_swapchainResult = VK_RESULT_MAX_ENUM;
	uint32_t m_swapchainImageIndex = 0u;

	VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE; // primary graphics command
	VkCommandPool m_graphicsPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> m_secondaryCommands;

	uint64_t transferWaitValue = UINT64_MAX;

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

	// Current transforms


	// Visible instances from directional light perspective
	std::vector<Instance> m_visibleShadowCasters;
	std::array<IndirectDrawRange, RD::MAX_SHADOW_CASCADES> m_shadowCasterDrawRanges;

	// Shadow casters for flashlight
	IndirectDrawRange m_flashlightShadowCasterDrawRange;

	IndirectDrawRange m_opaqueDrawRange;
	IndirectDrawRange m_transparentDrawRange;

	VisibilitySyncResult m_visibilitySyncResult;

	uint32_t m_cachedDrawExtentW = 0u;
	uint32_t m_cachedDrawExtentH = 0u;

	// Persistent light data buffers


	// Cluster shading buffers and management


	CMAA2Push m_cmaa2Push;

	AttachmentDesc m_graphicsAttachment;
	size_t m_gpuCopyStagingHead = 0u;

	VkQueryPool m_graphicsTimestampPool = VK_NULL_HANDLE;
	bool m_bHasTimestampResultsPending = false;
	std::array<PassTimestampRange, TIMESTAMP_PASS_COUNT> m_passTimestampRanges{};
	std::array<uint64_t, PASS_TIMESTAMP_QUERY_COUNT> m_timestampResults{};
	std::array<bool, TIMESTAMP_PASS_COUNT> m_timestampPassUsed{};

	// Culling data
	VisibilityPush m_visibilityPush{};
	uint32_t m_visibleCount = 0u;

	bool m_bTransformsInitialized = false;
	bool m_bTransformsBufferUploadNeeded = false;

	bool m_bLightsInitialized = false;
	bool m_bLightsBufferUploadNeeded = false;
	bool m_bRecentDynamicLightsTransform = false;

	// frame owned gpu buffers
	BindlessBDATable m_gpuAddressTable;
	uint32_t m_pendingAddressTableVersion = 0u;

	AllocatedBuffer m_directionalCSM_UBO;

	AllocatedBuffer m_sceneInfo_UBO;

	bool m_bClusterWriteNeeded = false;
	AllocatedBuffer m_clustered_UBO;

	VkDescriptorSet m_frameSet = VK_NULL_HANDLE;
	PushDescriptorWriter m_descriptorWriter;

	DeletionQueue m_cpuDeletionQueue;
};
