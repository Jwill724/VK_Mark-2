#pragma once

#include <common/Vk_Types.h>
#include "common/EngineTypes.h"
#include "common/EngineConstants.h"
#include "core/ResourceManager.h"
#include "gpu_types/Descriptor.h"

static uint32_t CURRENT_MSAA_LVL = MSAACOUNT_8;
static bool MSAA_ENABLED = true;

constexpr size_t OPAQUE_INDIRECT_SIZE_BYTES = MAX_OPAQUE_DRAWS * sizeof(VkDrawIndexedIndirectCommand);
constexpr size_t OPAQUE_INSTANCE_SIZE_BYTES = MAX_OPAQUE_DRAWS * sizeof(GPUInstance);
constexpr size_t TRANSPARENT_INSTANCE_SIZE_BYTES = MAX_TRANSPARENT_DRAWS * sizeof(GPUInstance);
constexpr size_t TRANSPARENT_INDIRECT_SIZE_BYTES = MAX_TRANSPARENT_DRAWS * sizeof(VkDrawIndexedIndirectCommand);

struct FrameContext {
	RenderSyncObjects syncObjs;
	uint32_t frameIndex = 0;

	VkResult swapchainResult;
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

	// Opaque draws
	std::vector<GPUInstance> opaqueInstances;
	AllocatedBuffer opaqueInstanceBuffer;
	std::vector<VkDrawIndexedIndirectCommand> opaqueIndirectDraws;
	AllocatedBuffer opaqueIndirectCmdBuffer;

	// Transparent draws
	std::vector<GPUInstance> transparentInstances;
	AllocatedBuffer transparentInstanceBuffer;
	std::vector<VkDrawIndexedIndirectCommand> transparentIndirectDraws;
	AllocatedBuffer transparentIndirectCmdBuffer;

	struct alignas(16) DrawPushConstants {
		uint32_t opaqueDrawCount;
		uint32_t transparentDrawCount;
		uint32_t totalVertexCount;
		uint32_t totalIndexCount;
	} drawData{};

	void clearRenderData() {
		opaqueInstances.clear();
		opaqueIndirectDraws.clear();
		transparentInstances.clear();
		transparentIndirectDraws.clear();
		transformsList.clear();
		opaqueVisibleCount = 0;
		transparentVisibleCount = 0;

		drawData.opaqueDrawCount = 0;
		drawData.transparentDrawCount = 0;
	}

	AllocatedBuffer combinedGPUStaging;

	// Culling data
	std::vector<uint32_t> visibleMeshIDs;
	// staging read back buffers
	AllocatedBuffer stagingVisibleMeshIDsBuffer;
	AllocatedBuffer stagingVisibleCountBuffer;
	std::atomic<bool> visibleCountInitialized = false; // one time init
	std::atomic<bool> meshDataSet = false;

	// gpu write only buffers
	AllocatedBuffer gpuVisibleCountBuffer;
	AllocatedBuffer gpuVisibleMeshIDsBuffer;

	CullingPushConstantsAddrs cullingPCData{};
	uint32_t visibleCount = 0;
	uint32_t opaqueVisibleCount = 0;
	uint32_t transparentVisibleCount = 0;

	// Transformations
	std::vector<glm::mat4> transformsList;
	AllocatedBuffer transformsListBuffer;

	//
	std::atomic<bool> refreshGlobalTransformList = true; // Set to false to indicate static transforms

	// Descriptor use
	GPUAddressTable addressTable;
	std::atomic<bool> addressTableDirty = false; // Always set to true when frame address table is updated
	AllocatedBuffer addressTableBuffer;
	AllocatedBuffer addressTableStaging;

	AllocatedBuffer sceneDataBuffer;

	VkDescriptorSet set = VK_NULL_HANDLE;
	DescriptorWriter writer;

	DeletionQueue cpuDeletion;
	TimelineDeletionQueue transferDeletion; // gpu buffer deletion
	TimelineDeletionQueue computeDeletion;
};

namespace Renderer {
	inline VkExtent3D _drawExtent;
	inline std::mutex drawExtentMutex;
	inline VkExtent3D getDrawExtent() {
		std::scoped_lock lock(drawExtentMutex);
		return _drawExtent;
	}
	inline void setDrawExtent(VkExtent3D extent) {
		std::scoped_lock lock(drawExtentMutex);
		_drawExtent = extent;
	}
	inline unsigned int _frameNumber{ 0 };

	inline FrameContext _frameContexts[MAX_FRAMES_IN_FLIGHT];

	inline std::mutex frameAccessMutex;
	inline FrameContext& getCurrentFrame() {
		std::scoped_lock lock(frameAccessMutex);
		return _frameContexts[_frameNumber % MAX_FRAMES_IN_FLIGHT];
	}

	// Timeline semaphore for tracking transfer and compute work
	extern TimelineSync _transferSync;
	extern TimelineSync _computeSync;

	void initFrameContexts(
		VkDevice device,
		VkDescriptorSetLayout layout,
		const VmaAllocator allocator,
		const uint32_t totalVertexCount,
		const uint32_t totalIndexCount,
		bool isAssetsLoaded = false);

	void recordRenderCommand(FrameContext& frameCtx);
	void prepareFrameContext(FrameContext& frameCtx);
	void submitFrame(FrameContext& frameCtx);

	void cleanup();
}