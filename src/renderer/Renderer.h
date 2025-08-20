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

namespace Renderer {
	const VkExtent3D getDrawExtent();
	void setDrawExtent(VkExtent3D extent);

	inline std::vector<std::unique_ptr<FrameContext>> _frameContexts;

	FrameContext& getCurrentFrame();

	// Timeline semaphore for tracking transfer and compute work
	extern TimelineSync _transferSync;
	extern TimelineSync _computeSync;

	void initRenderer(
		const VkDevice device,
		const VkDescriptorSetLayout frameLayout,
		GPUResources& gpuResouces,
		bool isAssetsLoaded = false);

	void recordRenderCommand(FrameContext& frameCtx, Profiler& profiler);
	void prepareFrameContext(FrameContext& frameCtx);
	void submitFrame(FrameContext& frameCtx);

	void cleanupRenderer(const VkDevice device, const VmaAllocator alloc);
}