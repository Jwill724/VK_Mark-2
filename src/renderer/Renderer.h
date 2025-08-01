#pragma once

#include "core/ResourceManager.h"
#include "gpu_types/Descriptor.h"
#include "profiler/Profiler.h"

constexpr size_t OPAQUE_INDIRECT_SIZE_BYTES = MAX_OPAQUE_DRAWS * sizeof(VkDrawIndexedIndirectCommand);
constexpr size_t OPAQUE_INSTANCE_SIZE_BYTES = MAX_OPAQUE_DRAWS * sizeof(GPUInstance);
constexpr size_t TRANSPARENT_INSTANCE_SIZE_BYTES = MAX_TRANSPARENT_DRAWS * sizeof(GPUInstance);
constexpr size_t TRANSPARENT_INDIRECT_SIZE_BYTES = MAX_TRANSPARENT_DRAWS * sizeof(VkDrawIndexedIndirectCommand);
constexpr size_t TRANSFORM_LIST_SIZE_BYTES = MAX_VISIBLE_TRANSFORMS * sizeof(glm::mat4);

struct FrameContext {
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

	std::vector<AllocatedBuffer> persistentGPUBuffers;

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
		uint32_t totalMeshCount;
		uint32_t totalMaterialCount;
		uint32_t pad0[2]{};
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

	std::atomic<bool> refreshGlobalTransformList = true; // Set to false to indicate static transforms

	// Descriptor use
	GPUAddressTable addressTable;
	std::atomic<bool> addressTableDirty = false; // Always set to true when frame address table is updated
	AllocatedBuffer addressTableBuffer;
	AllocatedBuffer addressTableStaging;

	AllocatedBuffer sceneDataBuffer;

	VkDescriptorSet set = VK_NULL_HANDLE;
	DescriptorWriter descriptorWriter;

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
	inline uint32_t _frameNumber{ 0 };

	inline std::vector<std::unique_ptr<FrameContext>> _frameContexts;
	inline uint32_t framesInFlight = 0;

	inline std::mutex frameAccessMutex;
	inline FrameContext& getCurrentFrame() {
		std::scoped_lock lock(frameAccessMutex);
		return *_frameContexts[_frameNumber % framesInFlight];
	}

	// Timeline semaphore for tracking transfer and compute work
	extern TimelineSync _transferSync;
	extern TimelineSync _computeSync;

	void initFrameContexts(
		VkDevice device,
		const VkDescriptorSetLayout frameLayout,
		GPUResources& gpuResouces,
		bool isAssetsLoaded = false);

	void recordRenderCommand(FrameContext& frameCtx, Profiler& profiler);
	void prepareFrameContext(FrameContext& frameCtx);
	void submitFrame(FrameContext& frameCtx);

	void cleanup();
}