#include "pch.h"
#include "Profiler.h"
#include "Engine.h"

void Profiler::enablePlatformTimerPrecision() {
	timeBeginPeriod(1);
	QueryPerformanceFrequency(&_qpcFreq);
}

void Profiler::disablePlatformTimerPrecision() {
	timeEndPeriod(1);
}

Profiler::~Profiler() {
	disablePlatformTimerPrecision();
}

void Profiler::beginFrame() {
	_frameStartTime = [&]() {
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		return static_cast<double>(now.QuadPart) / static_cast<double>(_qpcFreq.QuadPart);
	}();

	double now = _frameStartTime;
	double delta = now - _lastDeltaTime;
	_lastDeltaTime = now;

	float dt = std::min(static_cast<float>(delta), 0.1f);
	_stats.deltaTime = dt;
}

void Profiler::endFrame() {
	LARGE_INTEGER nowQPC;
	QueryPerformanceCounter(&nowQPC);
	double endTime = static_cast<double>(nowQPC.QuadPart) / static_cast<double>(_qpcFreq.QuadPart);
	float elapsed = static_cast<float>(endTime - _frameStartTime);

	if (_stats.capFramerate && _stats.targetFrameRate > 0.0f) {
		const float targetFrameTime = 1.0f / _stats.targetFrameRate;
		float timeLeft = targetFrameTime - elapsed;

		if (timeLeft > 0.002f) {
			Sleep(static_cast<DWORD>((timeLeft - 0.001f) * 1000.0f));
		}

		while (true) {
			QueryPerformanceCounter(&nowQPC);
			double current = static_cast<double>(nowQPC.QuadPart) / static_cast<double>(_qpcFreq.QuadPart);
			if ((current - _frameStartTime) >= targetFrameTime)
				break;
			_mm_pause();
		}

		QueryPerformanceCounter(&nowQPC);
		endTime = static_cast<double>(nowQPC.QuadPart) / static_cast<double>(_qpcFreq.QuadPart);
		elapsed = static_cast<float>(endTime - _frameStartTime);
	}

	_stats.frameTime = elapsed * 1000.0f;
	_stats.fps = 1.0f / std::max(elapsed, 0.00001f);
	_stats.deltaTime = elapsed;
}

void Profiler::startTimer() {
	QueryPerformanceCounter(&_startTimer);
}

float Profiler::endTimer() const {
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	auto elapsedTicks = now.QuadPart - _startTimer.QuadPart;
	return static_cast<float>(elapsedTicks) / static_cast<float>(_qpcFreq.QuadPart);
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