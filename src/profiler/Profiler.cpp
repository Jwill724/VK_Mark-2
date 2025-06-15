#include "pch.h"

#include "Profiler.h"

void Profiler::beginFrame() {
	_frameStart = std::chrono::high_resolution_clock::now();
}

void Profiler::endFrame() {
	auto end = std::chrono::high_resolution_clock::now();
	std::chrono::duration<float> delta = end - _frameStart;

	_stats.deltaTime = delta.count();
	_stats.frameTime = _stats.deltaTime * 1000.0f;
	_stats.fps = 1.0f / _stats.deltaTime;

	if (_stats.capFramerate) {
		float targetFrame = 1.0f / _stats.targetFrameRate;
		if (_stats.deltaTime < targetFrame) {
			std::this_thread::sleep_for(std::chrono::duration<float>(targetFrame - _stats.deltaTime));
		}
	}
}

GraphicsPipeline* Profiler::getPipelineByType(PipelineType type) const {
	switch (type) {
		case PipelineType::Opaque: return &Pipelines::opaquePipeline;
		case PipelineType::Transparent: return &Pipelines::transparentPipeline;
		case PipelineType::Wireframe: return &Pipelines::wireframePipeline;
		case PipelineType::BoundingBoxes: return &Pipelines::boundingBoxPipeline;
		default: return nullptr;
	}
}

VkDeviceSize Profiler::GetTotalVRAMUsage(VkPhysicalDevice device, VmaAllocator allocator) {
	VmaBudget budgets[VK_MAX_MEMORY_HEAPS];
	vmaGetHeapBudgets(allocator, budgets);

	VkPhysicalDeviceMemoryProperties memProps;
	vkGetPhysicalDeviceMemoryProperties(device, &memProps);

	VkDeviceSize totalUsage = 0;

	for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i) {
		const bool isDeviceLocal = memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

		VkDeviceSize usage = budgets[i].usage;
		VkDeviceSize budget = budgets[i].budget;

		fmt::print("[Heap {}] {} | Usage: {} MB / Budget: {} MB{}\n",
			i,
			isDeviceLocal ? "Device-local" : "Non-local",
			usage / (1024ull * 1024ull),
			budget / (1024ull * 1024ull),
			(usage > budget) ? "  [OVER BUDGET]" : "");

		fmt::print("[Heap {}] Flags = 0x{:x}, Size = {} MB\n",
			i,
			memProps.memoryHeaps[i].flags,
			memProps.memoryHeaps[i].size / (1024ull * 1024ull));

		if (isDeviceLocal) {
			totalUsage += usage;
		}
	}

	fmt::print("Total VRAM Usage (Device-local): {} MB\n", totalUsage / (1024ull * 1024ull));
	return totalUsage;
}
void Profiler::updateDeltaTime(float& lastFrameTime) {
	float currentTime = static_cast<float>(glfwGetTime());
	float deltaTime = currentTime - lastFrameTime;
	lastFrameTime = currentTime;

	_stats.deltaTime = deltaTime;
}