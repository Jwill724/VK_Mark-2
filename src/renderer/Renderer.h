#pragma once

#include <common/Vk_Types.h>
#include "common/EngineTypes.h"
#include "common/EngineConstants.h"
#include "core/ResourceManager.h"
#include "gpu/Descriptor.h"

struct FrameContext {
	RenderSyncObjects syncObjs;
	uint32_t frameIndex = 0;

	std::atomic<bool> ready = false;
	std::atomic<bool> inUse = false;

	VkResult swapchainResult;
	uint32_t swapchainImageIndex = 0;

	VkCommandBuffer commandBuffer = VK_NULL_HANDLE; // primary graphics command
	// Deferred transfer work
	VkCommandPool transferPool = VK_NULL_HANDLE;
	VkCommandPool graphicsPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> secondaryCmds;
	std::vector<VkCommandBuffer> transferCmds;
	uint64_t transferWaitValue = 0;
	VkFence transferFence = VK_NULL_HANDLE;

	// Data containers
	AllocatedBuffer instanceBuffer;
	AllocatedBuffer indirectCmdBuffer;
	std::vector<InstanceData> instanceData;
	std::vector<VkDrawIndexedIndirectCommand> indirectDraws;
	std::vector<RenderObject> opaqueRenderables;
	std::vector<RenderObject> transparentRenderables;

	// Descriptor use
	GPUAddressTable addressTable;
	AllocatedBuffer addressTableBuffer; // instance/indirect
	AllocatedBuffer addressTableStaging;

	AllocatedBuffer sceneDataBuffer;

	VkDescriptorSet set = VK_NULL_HANDLE;
	DescriptorWriter writer;

	DeletionQueue deletionQueue;
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

	extern TimelineSync _transferSync;

	void initFrameContexts(VkDevice device, uint32_t graphicsIndex, uint32_t transferIndex,
		VkExtent2D drawExtent, VkDescriptorSetLayout layout, const VmaAllocator allocator);

	void recordRenderCommand(FrameContext& frameCtx);
	void prepareFrameContext(FrameContext& frameCtx);
	void submitFrame(FrameContext& frameCtx, GPUResources& resources);

	void cleanup();
}