#pragma once

#include "core/Logging.h"
#include <functional>

struct ThreadContext;

class JobSystem final
{
public:
	static constexpr uint32_t MAIN_THREAD   = 0u;
	static constexpr uint32_t RENDER_THREAD = 1u;

	// Worker jobs — run on any available worker thread (2..N)
	void SubmitJob(std::function<void(ThreadContext&)> taskFn);
	void Wait();

	// Render thread — pinned to RENDER_THREAD (1), must be pumped by render loop
	void SubmitRenderJob(std::function<void(ThreadContext&)> taskFn);

	// Main thread — pinned to MAIN_THREAD (0), pump from main loop
	void SubmitMainJob(std::function<void(ThreadContext&)> taskFn);

	// Call once per loop tick on the thread that owns the role
	void PumpMainThread();
	void PumpRenderThread();

	ThreadContext& GetMainContext();
	ThreadContext& GetRenderContext();
	ThreadContext& GetThreadContext(uint32_t threadIndex);

	uint32_t GetThreadCount() const noexcept;
	uint32_t GetWorkerCount() const noexcept;   // excludes main + render

	Logging Logger;

	void Init()
	{
		if (!m_bIsInitialized)
		{
			InitScheduler();
			m_bIsInitialized = true;
		}
	}

	void Shutdown()
	{
		if (m_bIsInitialized)
		{
			ShutdownScheduler();
			m_bIsInitialized = false;
		}
	}
private:
	bool m_bIsInitialized = false;
	void InitScheduler();
	void ShutdownScheduler();
};
