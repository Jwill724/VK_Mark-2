#pragma once

#include "common/ResourceTypes.h"
#include "common/EngineTypes.h"
#include "renderer/gpu/Descriptor.h"

constexpr size_t MAX_GPU_INSTANCE_SIZE_BYTES = MAX_FRAME_INSTANCES_TOTAL * sizeof(GPUInstance);
constexpr size_t MAX_GPU_INDIRECT_SIZE_BYTES = MAX_FRAME_DRAW_COMMANDS_TOTAL * sizeof(VkDrawIndexedIndirectCommand);
constexpr size_t MAX_GPU_VISIBLE_IDS_BYTES = MAX_FRAME_INSTANCES_TOTAL * sizeof(uint32_t);

// Frames perform staging uploads on the global transforms buffer.
constexpr size_t TRANSFORMS_SIZE_BYTES = MAX_INSTANCE_TRANSFORMS * sizeof(glm::mat4);

constexpr uint32_t TIMESTAMP_QUERIES_PER_PASS = 2;
constexpr uint32_t TIMESTAMP_PASS_COUNT = static_cast<uint32_t>(PassID::Count);
constexpr uint32_t PASS_TIMESTAMP_QUERY_COUNT = TIMESTAMP_PASS_COUNT * TIMESTAMP_QUERIES_PER_PASS;

constexpr uint32_t FRAME_BEGIN_QUERY = PASS_TIMESTAMP_QUERY_COUNT + 0;
constexpr uint32_t FRAME_END_QUERY = PASS_TIMESTAMP_QUERY_COUNT + 1;
constexpr uint32_t TIMESTAMP_QUERY_COUNT = PASS_TIMESTAMP_QUERY_COUNT + 2;

struct FrameContext {
	uint32_t frameIndex = 0u;

	VkResult swapchainResult = VK_RESULT_MAX_ENUM;
	uint32_t swapchainImageIndex = 0u;

	VkCommandBuffer cmdBuffer = VK_NULL_HANDLE; // primary graphics command
	// Deferred transfer work
	VkCommandPool transferPool = VK_NULL_HANDLE;
	VkCommandPool graphicsPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> secondaryCmds;
	std::vector<VkCommandBuffer> transferCmds;
	uint64_t transferWaitValue = UINT64_MAX;

	// === async compute ===
	std::vector<VkCommandBuffer> computeCmds;
	VkCommandPool computePool = VK_NULL_HANDLE;
	uint64_t computeWaitValue = UINT64_MAX;

	std::vector<VkCommandBuffer> transferCmdsToFree;
	std::vector<VkCommandBuffer> computeCmdsToFree;
	std::vector<VkCommandBuffer> secondaryCmdsToFree;

	std::mutex submitMutex;
	void collectAndAppendCmds(std::vector<VkCommandBuffer>&& cmds, QueueType queue);
	void stashSubmitted(QueueType queue);
	void freeStashedCmds(const VkDevice device);

	std::vector<AllocatedBuffer> persistentGPUBuffers;

	// Flattened instance + command buffers
	std::vector<GPUInstance> visibleInstances;
	AllocatedBuffer visibleInstances_GPU;
	std::vector<VkDrawIndexedIndirectCommand> indirectDraws;
	AllocatedBuffer indirectDraws_GPU;

	AllocatedBuffer lights_GPU;
	uint32_t recentLightListCount = 0u;
	uint32_t uploadedFlashLightVersion = 0u;

	AllocatedBuffer transforms_GPU;
	AllocatedBuffer prevTransforms_GPU;

	// Visible instances from directional light perspective
	std::vector<GPUInstance> shadowCasterInstances;
	std::array<PassRange, MAX_SHADOW_CASCADES> shadowCastersRanges;
	std::array<PassRange, MAX_SHADOW_CASCADES> shadowDrawRanges;

	// Shadow casters for flashlight
	PassRange flashLightShadowRange;

	PassRange opaqueRange;
	PassRange transparentRange;

	VisibilitySyncResult visSyncResult;

	void clearRenderData() {
		visibleInstances.clear();
		indirectDraws.clear();
		visibleCount = 0;
		opaqueRange = {};
		transparentRange = {};
		shadowCasterInstances.clear();
		flashLightShadowRange = {};
		for (uint32_t i = 0; i < MAX_SHADOW_CASCADES; ++i) {
			shadowCastersRanges[i] = {};
			shadowDrawRanges[i] = {};
		}
	}

	uint32_t cachedExtentWidth = 0u;
	uint32_t cachedExtentHeight = 0u;

	// Persistent light data buffers
	AllocatedBuffer visibleLightCount_GPU;
	AllocatedBuffer visibleLightIDs_GPU;

	// Cluster shading buffers and management
	AllocatedBuffer clusterCounts_GPU;
	AllocatedBuffer clusterOffsets_GPU;
	AllocatedBuffer clusterCursors_GPU;
	AllocatedBuffer clusterLightIDs_GPU;
	AllocatedBuffer clusterTileSliceRanges_GPU;
	AllocatedBuffer clusterScanScratch_GPU;
	std::vector<AllocatedBuffer> clusterGPUBuffers;

	void clusterReset() {
		clusterGPUBuffers.clear();

		clusterCounts_GPU = {};
		clusterOffsets_GPU = {};
		clusterCursors_GPU = {};
		clusterLightIDs_GPU = {};
		clusterTileSliceRanges_GPU = {};
		clusterScanScratch_GPU = {};

		clusterGPUBuffers.reserve(6u);
	}
	void createClusterBuffers(
		const uint32_t extentWidth,
		const uint32_t extentHeight,
		const VmaAllocator alloc);


	CMAA2Push cmaa2Push;
	void createCMAA2Buffers(
		const uint32_t extentWidth,
		const uint32_t extentHeight,
		const VmaAllocator alloc);

	AllocatedBuffer cmaa2Control_GPU;
	AllocatedBuffer cmaa2ShapeCandidates_GPU;
	AllocatedBuffer cmaa2DeferredLocations_GPU;
	AllocatedBuffer cmaa2DeferredItems_GPU;
	AllocatedBuffer cmaa2DeferredHeads_GPU;
	std::vector<AllocatedBuffer> cmaa2GPUBuffers;

	void cmaa2Reset() {
		cmaa2GPUBuffers.clear();

		cmaa2Control_GPU = {};
		cmaa2ShapeCandidates_GPU = {};
		cmaa2DeferredLocations_GPU = {};
		cmaa2DeferredItems_GPU = {};
		cmaa2DeferredHeads_GPU = {};

		cmaa2GPUBuffers.reserve(5u);
	}

	AllocatedBuffer dispatchIndirectArgs_GPU;

	AttachmentDesc attachments;

	size_t stagingHead = 0u;
	AllocatedBuffer combinedGPUStaging;

	VkQueryPool graphicsTimestampPool = VK_NULL_HANDLE;
	bool hasTimestampResultsPending = false;
	std::array<PassTimestampRange, TIMESTAMP_PASS_COUNT> passTimestampRanges{};
	std::array<uint64_t, PASS_TIMESTAMP_QUERY_COUNT> timestampResults{};
	std::array<bool, TIMESTAMP_PASS_COUNT> timestampPassUsed{};

	const PassTimestampRange& getTimestampRange(PassID passID) const {
		return passTimestampRanges[static_cast<uint32_t>(passID)];
	}

	// Culling data
	VisibilityPush visPush{};
	uint32_t visibleCount = 0u;

	// TODO: account for dynamic model transforms
	bool transformsInitialized = false;
	bool transformsBufferUploadNeeded = false;

	bool lightsInitialized = false;
	bool lightsBufferUploadNeeded = false;
	bool recentDynamicLightsTransform = false;

	// Descriptor use
	GPUAddressTable addressTable{};
	AllocatedBuffer addressTable_GPU;
	void updateAddressTableIfDirty(const VkDevice device);
	uint32_t pendingAddressTableVersion = 0u;

	AllocatedBuffer shadowCSM_UBO;

	AllocatedBuffer sceneData_UBO;

	bool clusterWriteNeeded = false;
	AllocatedBuffer clustered_UBO;

	VkDescriptorSet set = VK_NULL_HANDLE;
	DescriptorWriter descriptorWriter;

	void writeFrameUniforms(const VkDevice device);

	void updateFrameSet(const VkDevice device);

	DeletionQueue cpuDeletion;
};

std::vector<std::unique_ptr<FrameContext>> initFrameContexts(
	const VkDevice device,
	const VkDescriptorSetLayout frameLayout,
	const VmaAllocator alloc,
	uint32_t& framesInFlight);

void cleanupFrameContexts(
	std::vector<std::unique_ptr<FrameContext>>& frameContexts,
	const VkDevice device,
	const VmaAllocator alloc);
