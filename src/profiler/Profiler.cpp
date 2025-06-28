#include "pch.h"

#include "Profiler.h"
#include "Engine.h"

void Profiler::beginFrame() {
	_frameStart = std::chrono::system_clock::now();
}

void Profiler::endFrame() {
	auto end = std::chrono::system_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - _frameStart);

	_stats.frameTime = static_cast<float>(elapsed.count()) / 1000.0f;
	_stats.fps = 1000.0f / _stats.frameTime;

	if (_stats.capFramerate) {
		float targetFrame = 1.0f / _stats.targetFrameRate; // seconds
		float deltaTimeSeconds = _stats.deltaTime / 1000.0f; // convert from ms to seconds

		if (deltaTimeSeconds < targetFrame) {
			std::this_thread::sleep_for(std::chrono::duration<float>(targetFrame - deltaTimeSeconds));
		}
	}
}

VkPipeline Profiler::getPipelineByType(PipelineType type) const {
	switch (type) {
		case PipelineType::Opaque: return Pipelines::opaquePipeline.pipeline;
		case PipelineType::Transparent: return Pipelines::transparentPipeline.pipeline;
		case PipelineType::Wireframe: return Pipelines::wireframePipeline.pipeline;
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
	const float currentTime = static_cast<float>(glfwGetTime());
	const float deltaTime = currentTime - lastFrameTime;
	lastFrameTime = currentTime;

	_stats.deltaTime = deltaTime;
}