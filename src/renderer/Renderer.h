#pragma once

#include "core/ResourceManager.h"
#include "gpu/Descriptor.h"
#include "gpu/CommandBuffer.h"
#include "gpu/PipelineManager.h"
#include "engine/platform/profiler/Profiler.h"
#include "renderer/backend/Backend.h"
#include "utils/ImageUtils.h"
#include "utils/BarrierUtils.h"
#include "renderer/frame/FrameContext.h"

using namespace FrameContext;

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

	inline std::vector<std::unique_ptr<FrameCtx>> _frameContexts;
	inline uint32_t framesInFlight = 0;

	inline std::mutex frameAccessMutex;
	inline FrameCtx& getCurrentFrame() {
		std::scoped_lock lock(frameAccessMutex);
		return *_frameContexts[_frameNumber % framesInFlight];
	}

	// Timeline semaphore for tracking transfer and compute work
	extern TimelineSync _transferSync;
	extern TimelineSync _computeSync;

	void initRenderer(
		const VkDevice device,
		const VkDescriptorSetLayout frameLayout,
		GPUResources& gpuResouces,
		bool isAssetsLoaded = false);

	void recordRenderCommand(FrameCtx& frameCtx, Profiler& profiler);
	void prepareFrameContext(FrameCtx& frameCtx);
	void submitFrame(FrameCtx& frameCtx);

	void cleanupRenderer(const VkDevice device, const VmaAllocator alloc);
}