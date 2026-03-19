#include "pch.h"

#include "engine/platform/profiler/Profiler.h"
#include "renderer/backend/Backend.h"

namespace {
	struct PassInfo {
		const char* displayName = "";
	};

	static constexpr std::array<PassInfo, static_cast<size_t>(PassID::Count)> PassInfoTable = {{
		{ "None" },
		{ "Pre-Pass" },
		{ "Hi-Z Generation" },
		{ "Clustered Light Build" },
		{ "GTAO" },
		{ "Directional CSM" },
		{ "Flashlight Shadow Map" },
		{ "SS Contact Shadows" },
		{ "Skybox" },
		{ "Opaque Forward" },
		{ "OBB Line View" },
		{ "Transparent Forward" },
		{ "Volumetric Lighting" },
		{ "TAA" },
		{ "Luminance Exposure" },
		{ "Lens Flare" },
		{ "Final Composite" },
		{ "CMAA2" },
		{ "SMAA" },
		{ "FXAA" },
		{ "Chromatic Aberration" },
	}};

	static int64_t queryPerformanceCounterTicks() {
		LARGE_INTEGER counter{};
		QueryPerformanceCounter(&counter);
		return counter.QuadPart;
	}

	static int64_t queryPerformanceFrequencyTicks() {
		LARGE_INTEGER frequency{};
		QueryPerformanceFrequency(&frequency);
		return frequency.QuadPart;
	}

#ifdef TRACY_ENABLE
	static constexpr tracy::SourceLocationData makePassSourceLocation(const char* name) {
		return tracy::SourceLocationData{
			name,
			"RenderPass",
			__FILE__,
			0,
			0
		};
	}

	// Order of execution
	static constexpr std::array<tracy::SourceLocationData, static_cast<size_t>(PassID::Count)> TracyPassSourceLocations = {{
		makePassSourceLocation("None"),
		makePassSourceLocation("Pre-Pass"),
		makePassSourceLocation("Hi-Z Generation"),
		makePassSourceLocation("Clustered Light Build"),
		makePassSourceLocation("GTAO"),
		makePassSourceLocation("Directional CSM"),
		makePassSourceLocation("Flashlight Shadow Map"),
		makePassSourceLocation("SS Contact Shadows"),
		makePassSourceLocation("Skybox"),
		makePassSourceLocation("Opaque Forward"),
		makePassSourceLocation("OBB Line View"),
		makePassSourceLocation("Transparent Forward"),
		makePassSourceLocation("Volumetric Lighting"),
		makePassSourceLocation("TAA"),
		makePassSourceLocation("Luminance Exposure"),
		makePassSourceLocation("Lens Flare"),
		makePassSourceLocation("Final Composite"),
		makePassSourceLocation("CMAA2"),
		makePassSourceLocation("SMAA"),
		makePassSourceLocation("FXAA"),
		makePassSourceLocation("Chromatic Aberration")
	}};
#endif
}

bool Profiler::isTracyCompiledIn() const
{
#ifdef TRACY_ENABLE
	return true;
#else
	return false;
#endif
}

Profiler::ScopedPass::ScopedPass(
	Profiler& profiler,
	FrameContext& frameCtx,
	VkCommandBuffer cmd,
	PassID passID)
	: _profiler(&profiler)
	, _frameCtx(&frameCtx)
	, _cmd(cmd)
	, _passID(passID)
{
	_profiler->markPassActive(_passID);
	_cpuStartTicks = queryPerformanceCounterTicks();
	_gpuZone = _profiler->beginTracyGpuPass(cmd, _passID);

	if (Backend::queueSupportsTimestamps(Backend::getGraphicsQueue()) &&
		_frameCtx->graphicsTimestampPool != VK_NULL_HANDLE)
	{
		const auto& range = _frameCtx->passTimestampRanges[static_cast<size_t>(_passID)];
		_frameCtx->timestampPassUsed[static_cast<size_t>(_passID)] = true;
		_frameCtx->hasTimestampResultsPending = true;
		vkCmdWriteTimestamp2(
			_cmd,
			VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
			_frameCtx->graphicsTimestampPool,
			range.beginQuery
		);
	}
}

Profiler::ScopedPass::ScopedPass(ScopedPass&& other) noexcept
{
	_profiler = other._profiler;
	_passID = other._passID;
	_gpuZone = other._gpuZone;
	_cpuStartTicks = other._cpuStartTicks;

	other._profiler = nullptr;
	other._passID = PassID::None;
	other._gpuZone = nullptr;
	other._cpuStartTicks = 0;
}

Profiler::ScopedPass& Profiler::ScopedPass::operator=(ScopedPass&& other) noexcept
{
	if (this == &other) {
		return *this;
	}

	if (_profiler != nullptr) {
		if (_cpuStartTicks != 0) {
			const int64_t endTicks = queryPerformanceCounterTicks();
			const int64_t elapsedTicks = endTicks - _cpuStartTicks;

			const float cpuMs =
				(static_cast<float>(elapsedTicks) * 1000.0f) /
				static_cast<float>(_profiler->_qpcFrequency);

			_profiler->addCpuPassTime(_passID, cpuMs);
		}

		if (_gpuZone != nullptr) {
			_profiler->endTracyGpuPass(_gpuZone);
		}
	}

	_profiler = other._profiler;
	_passID = other._passID;
	_gpuZone = other._gpuZone;
	_cpuStartTicks = other._cpuStartTicks;

	other._profiler = nullptr;
	other._passID = PassID::None;
	other._gpuZone = nullptr;
	other._cpuStartTicks = 0;

	return *this;
}

Profiler::ScopedPass::~ScopedPass()
{
	if (_profiler != nullptr) {
		if (_frameCtx != nullptr &&
			_cmd != VK_NULL_HANDLE &&
			Backend::queueSupportsTimestamps(Backend::getGraphicsQueue()) &&
			_frameCtx->graphicsTimestampPool != VK_NULL_HANDLE)
		{
			const auto& range = _frameCtx->passTimestampRanges[static_cast<size_t>(_passID)];

			vkCmdWriteTimestamp2(
				_cmd,
				VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
				_frameCtx->graphicsTimestampPool,
				range.endQuery
			);
		}

		if (_cpuStartTicks != 0) {
			const int64_t endTicks = queryPerformanceCounterTicks();
			const int64_t elapsedTicks = endTicks - _cpuStartTicks;

			const float cpuMs =
				(static_cast<float>(elapsedTicks) * 1000.0f) /
				static_cast<float>(_profiler->_qpcFrequency);

			_profiler->addCpuPassTime(_passID, cpuMs);
		}

		if (_gpuZone != nullptr) {
			_profiler->endTracyGpuPass(_gpuZone);
		}
	}
}

Profiler::Profiler()
{
	enablePlatformTimerPrecision();
	resetPassStats();
}

Profiler::~Profiler()
{
	shutdownTracyGPU();
	disablePlatformTimerPrecision();
}

void Profiler::enablePlatformTimerPrecision()
{
	timeBeginPeriod(1);

	_qpcFrequency = queryPerformanceFrequencyTicks();
	_qpcInverse = 1.0 / static_cast<double>(_qpcFrequency);
}

void Profiler::disablePlatformTimerPrecision()
{
	timeEndPeriod(1);
}

void Profiler::beginFrame()
{
	resetPassStats();

#ifdef TRACY_ENABLE
	FrameMarkStart("Renderer Frame");
#endif

	if (_stats.capFramerate && _stats.targetFrameRate > 0.0f) {
		_framePeriodTicks = llround(
			static_cast<double>(_qpcFrequency) / static_cast<double>(_stats.targetFrameRate)
		);

		if (_nextFrameTick == 0) {
			const int64_t nowTicks = queryPerformanceCounterTicks();
			_nextFrameTick = nowTicks + _framePeriodTicks;
		}
	}

	const int64_t nowTicks = queryPerformanceCounterTicks();
	_frameStartTime = static_cast<double>(nowTicks) * _qpcInverse;

	const double deltaSeconds = _frameStartTime - _lastFrameTime;
	_lastFrameTime = _frameStartTime;

	const double clampedDeltaSeconds = std::min(deltaSeconds, 0.1);
	_stats.deltaSecondsRaw = static_cast<float>(std::max(clampedDeltaSeconds, 0.0));

	_stats.deltaTime.add(_stats.deltaSecondsRaw);
	_stats.vramQueryTimerSeconds += _stats.deltaSecondsRaw;

	rendererWasStalled = (deltaSeconds > 0.05);
}

void Profiler::endFrame()
{
	const int64_t renderEndTicks = queryPerformanceCounterTicks();
	const double renderEndTime = static_cast<double>(renderEndTicks) * _qpcInverse;

	const float uncappedElapsedSeconds = static_cast<float>(renderEndTime - _frameStartTime);
	_stats.frameTimeRawMs = uncappedElapsedSeconds * 1000.0f;
	_stats.frameTimeRaw.add(_stats.frameTimeRawMs);

	const bool capEnabled = (_stats.capFramerate && _stats.targetFrameRate > 0.0f);

	if (capEnabled) {
		int64_t nowTicks = renderEndTicks;

		const int64_t twoMillisecondsTicks = _qpcFrequency / 500;
		const int64_t oneMillisecondTicks = _qpcFrequency / 1000;

		int64_t earlyTicks = _nextFrameTick - nowTicks;

		if (earlyTicks > twoMillisecondsTicks) {
			const int64_t sleepTicks = earlyTicks - oneMillisecondTicks;

			if (sleepTicks > 0) {
				const DWORD sleepMilliseconds = static_cast<DWORD>(
					(sleepTicks * 1000) / _qpcFrequency
				);

				if (sleepMilliseconds > 0) {
					Sleep(sleepMilliseconds);
				}

				nowTicks = queryPerformanceCounterTicks();
			}
		}

		if (nowTicks >= _nextFrameTick) {
			_nextFrameTick = nowTicks + _framePeriodTicks;
		}
		else {
			do {
				_mm_pause();
				nowTicks = queryPerformanceCounterTicks();
			} while (nowTicks < _nextFrameTick);

			_nextFrameTick += _framePeriodTicks;
		}
	}

	const int64_t presentedEndTicks = queryPerformanceCounterTicks();
	const double presentedEndTime = static_cast<double>(presentedEndTicks) * _qpcInverse;

	float displayedElapsedSeconds = static_cast<float>(presentedEndTime - _frameStartTime);

	if (capEnabled) {
		const double targetDeltaSeconds = 1.0 / static_cast<double>(_stats.targetFrameRate);

		if (displayedElapsedSeconds < static_cast<float>(targetDeltaSeconds * 0.999)) {
			displayedElapsedSeconds = static_cast<float>(targetDeltaSeconds);
		}
	}

	const float frameMilliseconds = displayedElapsedSeconds * 1000.0f;
	const float framesPerSecond = 1.0f / std::max(displayedElapsedSeconds, 0.00001f);

	_stats.frameTime.add(frameMilliseconds);
	_stats.fps.add(framesPerSecond);

#ifdef TRACY_ENABLE
	FrameMarkEnd("Renderer Frame");
#endif
}

void Profiler::startTimer()
{
	_startTimerTick = queryPerformanceCounterTicks();
}

float Profiler::endTimerMS() const
{
	const int64_t nowTicks = queryPerformanceCounterTicks();
	const int64_t elapsedTicks = nowTicks - _startTimerTick;

	return (static_cast<float>(elapsedTicks) * 1000.0f) / static_cast<float>(_qpcFrequency);
}

float Profiler::endTimerSec() const
{
	return endTimerMS() / 1000.0f;
}

void Profiler::resetDrawCalls()
{
	_stats.triangleCount = 0;
	_stats.directDraws = 0;

	_stats.opaqueIndirect.commands = 0;
	_stats.opaqueIndirect.subdraws = 0;

	_stats.transparentIndirect.commands = 0;
	_stats.transparentIndirect.subdraws = 0;

	_stats.directionalCSMIndirect.commands = 0;
	_stats.directionalCSMIndirect.subdraws = 0;

	_stats.flashlightShadowIndirect.commands = 0;
	_stats.flashlightShadowIndirect.subdraws = 0;
}

void Profiler::resetPassStats()
{
	for (PassTimingStats& passStats : _passStats) {
		passStats.activeLastFrame = passStats.activeThisFrame;
		passStats.activeThisFrame = false;
		passStats.cpuMsRaw = 0.0f;
		passStats.gpuMsRaw = 0.0f;
	}
}

void Profiler::markPassActive(PassID passID)
{
	auto& passStats = _passStats[static_cast<size_t>(passID)];
	passStats.activeThisFrame = true;
}

void Profiler::addCpuPassTime(
	PassID passID,
	float milliseconds)
{
	auto& passStats = _passStats[static_cast<size_t>(passID)];

	passStats.activeThisFrame = true;
	passStats.cpuMsRaw = milliseconds;
	passStats.cpuMsAverage.add(milliseconds);
}

void Profiler::addGpuPassTime(
	PassID passID,
	float milliseconds)
{
	auto& passStats = _passStats[static_cast<size_t>(passID)];

	passStats.activeThisFrame = true;
	passStats.gpuMsRaw = milliseconds;
	passStats.gpuMsAverage.add(milliseconds);
}

const char* Profiler::getPassName(PassID passID) const
{
	return PassInfoTable[static_cast<size_t>(passID)].displayName;
}

const std::array<PassTimingStats, Profiler::PassCount>& Profiler::getAllPassStats() const
{
	return _passStats;
}

bool Profiler::isPassActive(PassID passID) const
{
	return _passStats[static_cast<size_t>(passID)].activeThisFrame;
}

Profiler::ScopedPass Profiler::profilePass(
	FrameContext& frameCtx,
	VkCommandBuffer cmd,
	PassID passID)
{
	return ScopedPass(*this, frameCtx, cmd, passID);
}

void Profiler::initTracyGPU(
	VkPhysicalDevice physicalDevice,
	VkDevice device,
	VkQueue queue,
	VkCommandBuffer cmd)
{
#ifdef TRACY_ENABLE
	if (_tracyGpuContext != nullptr) return;

	TracyVkCtx tracyContext = TracyVkContext(
		physicalDevice,
		device,
		queue,
		cmd
	);

	_tracyGpuContext = tracyContext;
#else
	(void)physicalDevice;
	(void)device;
	(void)queue;
	(void)cmd;
#endif
}

void Profiler::shutdownTracyGPU()
{
#ifdef TRACY_ENABLE
	if (_tracyGpuContext == nullptr) return;

	TracyVkDestroy(static_cast<TracyVkCtx>(_tracyGpuContext));
	_tracyGpuContext = nullptr;
#endif
}

void Profiler::collectTracyGPU(VkCommandBuffer cmd)
{
#ifdef TRACY_ENABLE
	if (_tracyGpuContext == nullptr) return;

	TracyVkCollect(static_cast<TracyVkCtx>(_tracyGpuContext), cmd);
#else
	(void)cmd;
#endif
}

bool Profiler::isTracyGPUActive() const
{
#ifdef TRACY_ENABLE
	return _tracyGpuContext != nullptr;
#else
	return false;
#endif
}

void* Profiler::beginTracyGpuPass(
	VkCommandBuffer cmd,
	PassID passID)
{
#ifdef TRACY_ENABLE
	if (_tracyGpuContext == nullptr || cmd == VK_NULL_HANDLE) return nullptr;

	TracyVkCtx tracyContext = static_cast<TracyVkCtx>(_tracyGpuContext);
	const auto& srcLoc = TracyPassSourceLocations[static_cast<size_t>(passID)];

	return new tracy::VkCtxScope(
		tracyContext,
		&srcLoc,
		cmd,
		true
	);
#else
	(void)cmd;
	(void)passID;
	return nullptr;
#endif
}

void Profiler::endTracyGpuPass(void* gpuZone)
{
#ifdef TRACY_ENABLE
	if (gpuZone == nullptr) return;

	delete static_cast<tracy::VkCtxScope*>(gpuZone);
#else
	(void)gpuZone;
#endif
}

FrameStats& Profiler::getStats()
{
	return _stats;
}

const FrameStats& Profiler::getStats() const
{
	return _stats;
}

const PassTimingStats& Profiler::getPassStats(PassID passID) const
{
	return _passStats[static_cast<size_t>(passID)];
}

PassTimingStats& Profiler::getPassStats(PassID passID)
{
	return _passStats[static_cast<size_t>(passID)];
}

void Profiler::addDirect(
	uint32_t calls,
	uint64_t triangles)
{
	_stats.directDraws += calls;
	_stats.triangleCount += triangles;
}

void Profiler::addOpaqueIndirect(
	uint32_t commands,
	uint32_t subdraws,
	uint64_t triangles)
{
	_stats.opaqueIndirect.commands += commands;
	_stats.opaqueIndirect.subdraws += subdraws;
	_stats.triangleCount += triangles;
}

void Profiler::addTransparentIndirect(
	uint32_t commands,
	uint32_t subdraws,
	uint64_t triangles)
{
	_stats.transparentIndirect.commands += commands;
	_stats.transparentIndirect.subdraws += subdraws;
	_stats.triangleCount += triangles;
}

void Profiler::enableGPUAccelUsage()
{
	_gpuAccelOn = true;
}

void Profiler::disableGPUAccelUsage()
{
	_gpuAccelOn = false;
}

bool Profiler::isGPUAccelOn() const
{
	return _gpuAccelOn && Backend::isComputeAvailable();
}

VRAMStats Profiler::getTotalVRAMUsage(
	VkPhysicalDevice physicalDevice,
	VmaAllocator allocator)
{
	VmaBudget budgets[VK_MAX_MEMORY_HEAPS];
	vmaGetHeapBudgets(allocator, budgets);

	VkPhysicalDeviceMemoryProperties memoryProperties{};
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

	VRAMStats stats{};

	for (uint32_t heapIndex = 0; heapIndex < memoryProperties.memoryHeapCount; ++heapIndex) {
		const bool isDeviceLocal =
			(memoryProperties.memoryHeaps[heapIndex].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;

		if (!isDeviceLocal) {
			continue;
		}

		stats.used += budgets[heapIndex].usage;
		stats.budget += budgets[heapIndex].budget;
	}

	return stats;
}
