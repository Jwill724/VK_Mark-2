#include "pch.h"

#include "Profiler.h"
#include "engine/Engine.h"

void Profiler::enablePlatformTimerPrecision() {
	timeBeginPeriod(1);
	QueryPerformanceFrequency(&_qpcFreq);
	_qpcFreqLL = _qpcFreq.QuadPart;
	_qpcInv = 1.0 / static_cast<double>(_qpcFreqLL);
}

Profiler::Profiler() {
	enablePlatformTimerPrecision();
}

void Profiler::disablePlatformTimerPrecision() {
	timeEndPeriod(1);
}

Profiler::~Profiler() {
	disablePlatformTimerPrecision();
}

void Profiler::beginFrame() {
	if (_stats.capFramerate && _stats.targetFrameRate > 0.0f) {
		_periodLL = llround(static_cast<double>(_qpcFreqLL) / static_cast<double>(_stats.targetFrameRate));
		if (_nextTickLL == 0) {
			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);
			_nextTickLL = now.QuadPart + _periodLL;
		}
	}

	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	_frameStartTime = static_cast<double>(now.QuadPart * _qpcInv);

	double delta = _frameStartTime - _lastDeltaTime;
	_lastDeltaTime = _frameStartTime;

	_stats.deltaTime = static_cast<float>(std::min(delta, 0.1));
	rendererWasStalled = (delta > 0.05); // 50ms stall
}

void Profiler::endFrame() {
	const bool capOn = (_stats.capFramerate && _stats.targetFrameRate > 0.0f);

	// schedule-based limiter in integer QPC ticks
	if (capOn) {
		LARGE_INTEGER tq;
		QueryPerformanceCounter(&tq);
		long long now = tq.QuadPart;

		// coarse sleep if early by > ~2 ms
		const long long twoMs = _qpcFreqLL / 500; // 2 ms worth of ticks
		long long earlyTicks = _nextTickLL - now;
		if (earlyTicks > twoMs) {
			const long long leave = _qpcFreqLL / 1000; // wake ~1 ms early
			long long sleepTicks = earlyTicks - leave;
			if (sleepTicks > 0) {
				DWORD ms = DWORD((sleepTicks * 1000) / _qpcFreqLL);
				if (ms) Sleep(ms);
				QueryPerformanceCounter(&tq);
				now = tq.QuadPart;
			}
		}

		if (now >= _nextTickLL) {
			// missed (or exactly hit) this tick: schedule the next one
			_nextTickLL = now + _periodLL;
		}
		else {
			// short spin to the tick
			do {
				_mm_pause();
				QueryPerformanceCounter(&tq);
				now = tq.QuadPart;
			} while (now < _nextTickLL);
			_nextTickLL += _periodLL;
		}
	}

	// frame timing readout (seconds)
	LARGE_INTEGER endQ;
	QueryPerformanceCounter(&endQ);
	double endTime = static_cast<double>(endQ.QuadPart) * _qpcInv;
	float elapsed = static_cast<float>(endTime - _frameStartTime);

	// ui fps cap
	if (capOn) {
		const double targetDt = 1.0 / double(_stats.targetFrameRate);
		if (elapsed < static_cast<float>(targetDt * 0.999)) {
			elapsed = static_cast<float>(targetDt);
		}
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
	return static_cast<float>(elapsedTicks / _qpcFreq.QuadPart);
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