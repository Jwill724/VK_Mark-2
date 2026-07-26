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

	void SubmitRenderJob(std::function<void(ThreadContext&)> taskFn);

	void SubmitMainJob(std::function<void(ThreadContext&)> taskFn);

	void PumpMainThread();
	void PumpRenderThread();

	ThreadContext& GetMainContext();
	ThreadContext& GetRenderContext();
	ThreadContext& GetThreadContext(uint32_t threadIndex);

	uint32_t GetThreadCount() const noexcept;
	uint32_t GetWorkerCount() const noexcept;

	void RunParallel(
		uint32_t count,
		const std::function<void(ThreadContext&, uint32_t)>& fn);

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
