#pragma once

#include "common/ResourceTypes.h"
#include "common/EngineTypes.h"

constexpr size_t INSTANCE_SIZE_BYTES = MAX_DRAWS * sizeof(GPUInstance);
constexpr size_t INDIRECT_SIZE_BYTES = MAX_DRAWS * sizeof(VkDrawIndexedIndirectCommand);

// Frames perform staging uploads on the global transforms buffer.
constexpr size_t TRANSFORMS_SIZE_BYTES = MAX_VISIBLE_TRANSFORMS * sizeof(glm::mat4);

struct FrameContext {
	uint32_t frameIndex = 0;

	VkResult swapchainResult = VK_RESULT_MAX_ENUM;
	uint32_t swapchainImageIndex = 0;

	VkCommandBuffer commandBuffer = VK_NULL_HANDLE; // primary graphics command
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
	AllocatedBuffer visibleInstancesBuffer;
	std::vector<VkDrawIndexedIndirectCommand> indirectDraws;
	AllocatedBuffer indirectDrawsBuffer;

	PassRange opaqueRange;
	PassRange transparentRange;

	VisibilitySyncResult visSyncResult;

	struct alignas(16) DrawPushConstants {
		uint32_t totalVertexCount;
		uint32_t totalIndexCount;
		uint32_t totalMeshCount;
		uint32_t totalMaterialCount;
	} drawDataPC{};

	void clearRenderData() {
		visibleInstances.clear();
		indirectDraws.clear();
		visibleCount = 0;
		opaqueRange = {};
		transparentRange = {};
	}

	AllocatedBuffer combinedGPUStaging;

	// Culling data
	CullingPushConstantsAddrs cullingPCData{};
	uint32_t visibleCount = 0;

	// frames can update the global static transforms
	bool staticTransformsUploadNeeded = false;

	// Descriptor use
	GPUAddressTable addressTable{};
	// Determines if a write is required, set to false during set write
	bool addressTableDirty = false; // Always set to true when frame address table is updated
	AllocatedBuffer addressTableBuffer;

	AllocatedBuffer sceneDataBuffer;

	VkDescriptorSet set = VK_NULL_HANDLE;
	DescriptorWriter descriptorWriter;

	// Only write once per frame
	void writeFrameDescriptors(const VkDevice device);

	DeletionQueue cpuDeletion;
};

std::vector<std::unique_ptr<FrameContext>> initFrameContexts(
	const VkDevice device,
	const VkDescriptorSetLayout frameLayout,
	const VmaAllocator alloc,
	const ResourceStats resStats,
	uint32_t& framesInFlight,
	bool isAssetsLoaded = false);

void cleanupFrameContexts(
	std::vector<std::unique_ptr<FrameContext>>& frameContexts,
	const VkDevice device,
	const VmaAllocator alloc);