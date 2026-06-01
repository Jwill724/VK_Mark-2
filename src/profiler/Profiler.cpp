#include "pch.h"

#include "Profiler.h"
#include "renderer/Frame/FrameContext.h"

// -----------------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------------
namespace
{
	static int64_t queryPerformanceCounterTicks()
	{
		LARGE_INTEGER counter{};
		QueryPerformanceCounter(&counter);
		return counter.QuadPart;
	}

	static int64_t queryPerformanceFrequencyTicks()
	{
		LARGE_INTEGER frequency{};
		QueryPerformanceFrequency(&frequency);
		return frequency.QuadPart;
	}
}

// -----------------------------------------------------------------------------
// Profiler lifetime
// -----------------------------------------------------------------------------
Profiler::Profiler()
{
	EnablePlatformTimerPrecision();
	ResetPassStats();
}

Profiler::~Profiler()
{
	ShutdownTracyGPU();
	DisablePlatformTimerPrecision();
}

void Profiler::EnablePlatformTimerPrecision()
{
	timeBeginPeriod(1);
	m_qpcFrequency = queryPerformanceFrequencyTicks();
	m_qpcInverse   = 1.0 / static_cast<double>(m_qpcFrequency);
}

void Profiler::DisablePlatformTimerPrecision()
{
	timeEndPeriod(1);
}

// -----------------------------------------------------------------------------
// Frame lifecycle
// -----------------------------------------------------------------------------
void Profiler::BeginFrame()
{
	ResetPassStats();

#ifdef TRACY_ENABLE
	FrameMarkStart("Renderer Frame");
#endif

	if (m_stats.capFramerate && m_stats.targetFrameRate > 0.0f)
	{
		m_framePeriodTicks = llround(
			static_cast<double>(m_qpcFrequency) / static_cast<double>(m_stats.targetFrameRate)
		);

		if (m_nextFrameTick == 0)
		{
			m_nextFrameTick = queryPerformanceCounterTicks() + m_framePeriodTicks;
		}
	}

	const int64_t nowTicks  = queryPerformanceCounterTicks();
	m_frameStartTime        = static_cast<double>(nowTicks) * m_qpcInverse;

	const double deltaSeconds       = m_frameStartTime - m_lastFrameTime;
	m_lastFrameTime                 = m_frameStartTime;

	const double clampedDelta       = std::min(deltaSeconds, 0.1);
	m_stats.deltaSecondsRaw         = static_cast<float>(std::max(clampedDelta, 0.0));

	m_stats.deltaTime.Add(m_stats.deltaSecondsRaw);
	m_stats.vramQueryTimerSeconds  += m_stats.deltaSecondsRaw;

	rendererWasStalled = (deltaSeconds > 0.05);
}

void Profiler::EndFrame()
{
	const int64_t renderEndTicks  = queryPerformanceCounterTicks();
	const double  renderEndTime   = static_cast<double>(renderEndTicks) * m_qpcInverse;

	m_stats.frameTimeRawMs = static_cast<float>(renderEndTime - m_frameStartTime) * 1000.0f;
	m_stats.frameTimeRaw.Add(m_stats.frameTimeRawMs);

	const bool capEnabled = (m_stats.capFramerate && m_stats.targetFrameRate > 0.0f);

	if (capEnabled)
	{
		int64_t nowTicks = renderEndTicks;

		const int64_t twoMsTicks = m_qpcFrequency / 500;
		const int64_t oneMsTicks = m_qpcFrequency / 1000;

		const int64_t earlyTicks = m_nextFrameTick - nowTicks;

		if (earlyTicks > twoMsTicks)
		{
			const int64_t sleepTicks = earlyTicks - oneMsTicks;

			if (sleepTicks > 0)
			{
				const DWORD sleepMs = static_cast<DWORD>((sleepTicks * 1000) / m_qpcFrequency);
				if (sleepMs > 0)
					Sleep(sleepMs);

				nowTicks = queryPerformanceCounterTicks();
			}
		}

		if (nowTicks >= m_nextFrameTick)
		{
			m_nextFrameTick = nowTicks + m_framePeriodTicks;
		}
		else
		{
			do {
				_mm_pause();
				nowTicks = queryPerformanceCounterTicks();
			} while (nowTicks < m_nextFrameTick);

			m_nextFrameTick += m_framePeriodTicks;
		}
	}

	const int64_t presentedTicks = queryPerformanceCounterTicks();
	const double  presentedTime  = static_cast<double>(presentedTicks) * m_qpcInverse;

	float displayedSeconds = static_cast<float>(presentedTime - m_frameStartTime);

	if (capEnabled)
	{
		const float targetSeconds = 1.0f / m_stats.targetFrameRate;
		if (displayedSeconds < targetSeconds * 0.999f)
			displayedSeconds = targetSeconds;
	}

	m_stats.frameTime.Add(displayedSeconds * 1000.0f);
	m_stats.fps.Add(1.0f / std::max(displayedSeconds, 0.00001f));

#ifdef TRACY_ENABLE
	FrameMarkEnd("Renderer Frame");
#endif
}

Profiler::ScopedPass::ScopedPass(
	Profiler&                 profiler,
	FrameContext&             frameCtx,
	VkCommandBuffer           cmd,
	RD::Renderer_Pass         ID,
	std::string_view          passName)
	: m_profiler(&profiler)
	, m_frameCtx(&frameCtx)
	, m_cmd(cmd)
	, m_trackingID(ID)
{
	auto& stats           = m_profiler->m_passStats[static_cast<size_t>(ID)];
	stats.activeThisFrame = true;
	stats.name            = passName;

	m_cpuStartTicks = queryPerformanceCounterTicks();
	m_gpuZone       = m_profiler->BeginTracyGpuZone(cmd, ID);

	if (m_frameCtx && m_cmd != VK_NULL_HANDLE)
	{
		const size_t idx   = static_cast<size_t>(ID);
		const auto&  range = m_frameCtx->m_passTimestampRanges[idx];

		ASSERT(range.beginQuery < range.endQuery);

		m_frameCtx->m_timestampPassUsed[idx]     = true;
		m_frameCtx->m_bHasTimestampResultsPending = true;

		vkCmdWriteTimestamp2(
			m_cmd,
			VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
			m_frameCtx->m_graphicsTimestampPool,
			range.beginQuery);
	}
}

Profiler::ScopedPass::~ScopedPass()
{
	if (m_profiler == nullptr) return;

	if (m_frameCtx != nullptr && m_cmd != VK_NULL_HANDLE)
	{
		const size_t idx   = static_cast<size_t>(m_trackingID);
		const auto&  range = m_frameCtx->m_passTimestampRanges[idx];

		ASSERT(range.beginQuery < range.endQuery);

		vkCmdWriteTimestamp2(
			m_cmd,
			VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
			m_frameCtx->m_graphicsTimestampPool,
			range.endQuery);
	}

	if (m_cpuStartTicks != 0)
	{
		const int64_t endTicks = queryPerformanceCounterTicks();
		const float   cpuMs    =
			(static_cast<float>(endTicks - m_cpuStartTicks) * 1000.0f) /
			static_cast<float>(m_profiler->m_qpcFrequency);

		auto& stats    = m_profiler->m_passStats[static_cast<size_t>(m_trackingID)];
		stats.cpuMsRaw = cpuMs;
		stats.cpuMsAverage.Add(cpuMs);
	}

	m_profiler->EndTracyGpuZone(m_gpuZone);
}

Profiler::ScopedPass::ScopedPass(ScopedPass&& other) noexcept
	: m_profiler(other.m_profiler)
	, m_frameCtx(other.m_frameCtx)
	, m_cmd(other.m_cmd)
	, m_trackingID(other.m_trackingID)
	, m_gpuZone(other.m_gpuZone)
	, m_cpuStartTicks(other.m_cpuStartTicks)
{
	other.m_profiler      = nullptr;
	other.m_frameCtx      = nullptr;
	other.m_cmd           = VK_NULL_HANDLE;
	other.m_trackingID    = RD::Renderer_Pass::Count;
	other.m_gpuZone       = nullptr;
	other.m_cpuStartTicks = 0;
}

Profiler::ScopedPass& Profiler::ScopedPass::operator=(ScopedPass&& other) noexcept
{
	if (this == &other) return *this;

	this->~ScopedPass();

	m_profiler      = other.m_profiler;
	m_frameCtx      = other.m_frameCtx;
	m_cmd           = other.m_cmd;
	m_trackingID    = other.m_trackingID;
	m_gpuZone       = other.m_gpuZone;
	m_cpuStartTicks = other.m_cpuStartTicks;

	other.m_profiler      = nullptr;
	other.m_frameCtx      = nullptr;
	other.m_cmd           = VK_NULL_HANDLE;
	other.m_trackingID    = RD::Renderer_Pass::Count;
	other.m_gpuZone       = nullptr;
	other.m_cpuStartTicks = 0;

	return *this;
}

Profiler::ScopedPass Profiler::ProfilePass(
	FrameContext&             frameCtx,
	VkCommandBuffer           cmd,
	RD::Renderer_Pass trackingID,
	std::string_view          passName)
{
	return ScopedPass(*this, frameCtx, cmd, trackingID, passName);
}

// -----------------------------------------------------------------------------
// Tracy GPU zone — indexed by Renderer_PassTracking
// -----------------------------------------------------------------------------
void* Profiler::BeginTracyGpuZone(VkCommandBuffer cmd, RD::Renderer_Pass trackingID)
{
#ifdef TRACY_ENABLE
	if (m_tracyGpuContext == nullptr || cmd == VK_NULL_HANDLE) return nullptr;

	const size_t   idx   = static_cast<size_t>(trackingID);
	TracyPassEntry& entry = m_tracySourceLocations[idx];

	if (!entry.srcLoc)
	{
		entry.name   = m_passStats[idx].name;
		entry.srcLoc = std::make_unique<tracy::SourceLocationData>(
			tracy::SourceLocationData{ entry.name.c_str(), "RenderPass", __FILE__, 0, 0 }
		);
	}

	return new tracy::VkCtxScope(
		static_cast<TracyVkCtx>(m_tracyGpuContext),
		entry.srcLoc.get(),
		cmd,
		true
	);
#else
	(void)cmd; (void)trackingID;
	return nullptr;
#endif
}

void Profiler::EndTracyGpuZone(void* zone)
{
#ifdef TRACY_ENABLE
	if (zone == nullptr) return;
	delete static_cast<tracy::VkCtxScope*>(zone);
#else
	(void)zone;
#endif
}

// -----------------------------------------------------------------------------
// GPU timestamp accumulation — called from FrameContext timestamp resolve
// -----------------------------------------------------------------------------
void Profiler::AddGpuPassTime(RD::Renderer_Pass trackingID, float milliseconds)
{
	auto& stats    = m_passStats[static_cast<size_t>(trackingID)];
	stats.gpuMsRaw += milliseconds;
	stats.gpuMsAverage.Add(milliseconds);
}

// -----------------------------------------------------------------------------
// Stats reset
// -----------------------------------------------------------------------------
void Profiler::ResetPassStats()
{
	for (auto& stats : m_passStats)
	{
		stats.activeLastFrame = stats.activeThisFrame;
		stats.activeThisFrame = false;
		stats.cpuMsRaw        = 0.0f;
		stats.gpuMsRaw        = 0.0f;
	}
}

// -----------------------------------------------------------------------------
// Stats accessors
// -----------------------------------------------------------------------------
const PassTimingStats& Profiler::GetPassStats(RD::Renderer_Pass trackingID) const
{
	return m_passStats[static_cast<size_t>(trackingID)];
}

PassTimingStats& Profiler::GetPassStats(RD::Renderer_Pass trackingID)
{
	return m_passStats[static_cast<size_t>(trackingID)];
}

// -----------------------------------------------------------------------------
// General-purpose CPU timer
// -----------------------------------------------------------------------------
void Profiler::StartTimer()
{
	m_startTimerTick = queryPerformanceCounterTicks();
}

float Profiler::EndTimerMS() const
{
	const int64_t elapsed = queryPerformanceCounterTicks() - m_startTimerTick;
	return (static_cast<float>(elapsed) * 1000.0f) / static_cast<float>(m_qpcFrequency);
}

float Profiler::EndTimerSec() const
{
	return EndTimerMS() / 1000.0f;
}

// -----------------------------------------------------------------------------
// Draw call counters
// -----------------------------------------------------------------------------
void Profiler::ResetDrawCalls()
{
	m_stats.triangleCount                    = 0;
	m_stats.directDraws                      = 0;
	m_stats.opaqueIndirect.commands          = 0;
	m_stats.opaqueIndirect.subdraws          = 0;
	m_stats.transparentIndirect.commands     = 0;
	m_stats.transparentIndirect.subdraws     = 0;
	m_stats.directionalCSMIndirect.commands  = 0;
	m_stats.directionalCSMIndirect.subdraws  = 0;
	m_stats.flashlightShadowIndirect.commands = 0;
	m_stats.flashlightShadowIndirect.subdraws = 0;
}

void Profiler::AddDirect(uint32_t calls, uint64_t triangles)
{
	m_stats.directDraws  += calls;
	m_stats.triangleCount += triangles;
}

void Profiler::AddOpaqueIndirect(uint32_t commands, uint32_t subdraws, uint64_t triangles)
{
	m_stats.opaqueIndirect.commands += commands;
	m_stats.opaqueIndirect.subdraws += subdraws;
	m_stats.triangleCount           += triangles;
}

void Profiler::AddTransparentIndirect(uint32_t commands, uint32_t subdraws, uint64_t triangles)
{
	m_stats.transparentIndirect.commands += commands;
	m_stats.transparentIndirect.subdraws += subdraws;
	m_stats.triangleCount                += triangles;
}

// -----------------------------------------------------------------------------
// Frame stats accessors
// -----------------------------------------------------------------------------
FrameStats& Profiler::getStats()             { return m_stats; }
const FrameStats& Profiler::getStats() const { return m_stats; }

// -----------------------------------------------------------------------------
// Tracy GPU context
// -----------------------------------------------------------------------------
bool Profiler::IsTracyCompiledIn() const
{
#ifdef TRACY_ENABLE
	return true;
#else
	return false;
#endif
}

void Profiler::InitTracyGPU(
	VkPhysicalDevice physicalDevice,
	VkDevice         device,
	VkQueue          queue,
	VkCommandBuffer  cmd)
{
#ifdef TRACY_ENABLE
	if (m_tracyGpuContext != nullptr) return;
	m_tracyGpuContext = TracyVkContext(physicalDevice, device, queue, cmd);
#else
	(void)physicalDevice; (void)device; (void)queue; (void)cmd;
#endif
}

void Profiler::ShutdownTracyGPU()
{
#ifdef TRACY_ENABLE
	if (m_tracyGpuContext == nullptr) return;
	TracyVkDestroy(static_cast<TracyVkCtx>(m_tracyGpuContext));
	m_tracyGpuContext = nullptr;
#endif
}

void Profiler::CollectTracyGPU(VkCommandBuffer cmd)
{
#ifdef TRACY_ENABLE
	if (m_tracyGpuContext == nullptr) return;
	TracyVkCollect(static_cast<TracyVkCtx>(m_tracyGpuContext), cmd);
#else
	(void)cmd;
#endif
}

bool Profiler::IsTracyGPUActive() const noexcept
{
#ifdef TRACY_ENABLE
	return m_tracyGpuContext != nullptr;
#else
	return false;
#endif
}
