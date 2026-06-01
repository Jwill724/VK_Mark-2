#include "pch.h"

#include "JobSystem.h"
#include "EngineTypes.h"

static enki::TaskScheduler Scheduler;
static std::vector<ThreadContext> ThreadContexts;
static std::vector<enki::ICompletable*> s_pendingTasks;

// Each OS thread sets this to point at its own ThreadContext
static thread_local ThreadContext* tl_currentContext = nullptr;

// ---------------------------------------------------------------------------
// Internal task types
// ---------------------------------------------------------------------------

class WorkerTask : public enki::ITaskSet
{
public:
	WorkerTask(std::function<void(ThreadContext&)> fn)
		: ITaskSet(1), m_fn(std::move(fn)) {}

	void ExecuteRange(enki::TaskSetPartition, uint32_t threadIndex) override
	{
		// enki thread 0   -> ThreadContexts[0]  (main, can help execute tasks)
		// enki thread 1..N -> ThreadContexts[2..N+1] (workers, shifted past render slot)
		const uint32_t ourIdx = (threadIndex == 0u)
			? JobSystem::MAIN_THREAD    // main thread is helping out
			: threadIndex + 1u;         // worker: shift past the render slot

		// ourIdx will be 0 (main helping) or 2+ (workers). Never 1 (render). Safe.
		ThreadContext& ctx = ThreadContexts[ourIdx];
		ctx.jobsExecuted++;
		m_fn(ctx);
	}

private:
	std::function<void(ThreadContext&)> m_fn;
};

class PinnedTask : public enki::IPinnedTask
{
public:
	PinnedTask(std::function<void(ThreadContext&)> fn, uint32_t threadNum)
		: IPinnedTask(threadNum), m_fn(std::move(fn)) {}

	void Execute() override
	{
		ThreadContext& ctx = ThreadContexts[threadNum];
		ctx.jobsExecuted++;
		m_fn(ctx);
		delete this;
	}

private:
	std::function<void(ThreadContext&)> m_fn;
};

// ---------------------------------------------------------------------------
// Init / Shutdown
// ---------------------------------------------------------------------------

void JobSystem::InitScheduler()
{
	enki::TaskSchedulerConfig config;

	const uint32_t hw = std::thread::hardware_concurrency();
	const uint32_t enkiWorkers = (hw > 2u) ? hw - 2u : 1u;
	config.numTaskThreadsToCreate = enkiWorkers;

	Scheduler.Initialize(config);

	// Enki total thread count: 1 (main/thread-0) + enkiWorkers
	const uint32_t enkiTotal = Scheduler.GetNumTaskThreads();

	// Our ThreadContexts layout:
	//   [0]          = MAIN_THREAD   (enki thread 0)
	//   [1]          = RENDER_THREAD (NOT an enki thread — owned by you)
	//   [2..enkiTotal] = Worker threads (enki threads 1..enkiTotal-1)
	//
	// Total slots = enkiTotal + 1  (the +1 is the render thread slot)
	ThreadContexts.resize(enkiTotal + 1);

	{
		auto& ctx      = ThreadContexts[MAIN_THREAD];
		ctx.threadID   = MAIN_THREAD;
		ctx.threadRole = ThreadRole::Main;
	}

	{
		auto& ctx      = ThreadContexts[RENDER_THREAD];
		ctx.threadID   = RENDER_THREAD;
		ctx.threadRole = ThreadRole::Render;
	}

	for (uint32_t i = 1; i < enkiTotal; ++i)
	{
		uint32_t ourIdx    = i + 1;
		auto& ctx          = ThreadContexts[ourIdx];
		ctx.threadID       = ourIdx;
		ctx.threadRole     = ThreadRole::Worker;
	}

	Logger.Print("[JobSystem] Initialized: 1 main, 1 render, {} workers", GetWorkerCount());
}

void JobSystem::ShutdownScheduler()
{
	Scheduler.WaitforAllAndShutdown();
}

// ---------------------------------------------------------------------------
// Submission
// ---------------------------------------------------------------------------

void JobSystem::SubmitJob(std::function<void(ThreadContext&)> taskFn)
{
	auto* task = new WorkerTask(std::move(taskFn));
	s_pendingTasks.push_back(task);
	Scheduler.AddTaskSetToPipe(task);
}

void JobSystem::SubmitRenderJob(std::function<void(ThreadContext&)> taskFn)
{
	auto* task = new PinnedTask(std::move(taskFn), RENDER_THREAD);
	Scheduler.AddPinnedTask(task);
}

void JobSystem::SubmitMainJob(std::function<void(ThreadContext&)> taskFn)
{
	auto* task = new PinnedTask(std::move(taskFn), MAIN_THREAD);
	Scheduler.AddPinnedTask(task);
}

// ---------------------------------------------------------------------------
// Pumping — call these once per tick on the owning thread
// ---------------------------------------------------------------------------

void JobSystem::PumpMainThread()
{
	Scheduler.RunPinnedTasks();
}

void JobSystem::PumpRenderThread()
{
	Scheduler.RunPinnedTasks();
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

void JobSystem::Wait()
{
	Scheduler.WaitforAll();
	for (auto* t : s_pendingTasks)
		delete t;
	s_pendingTasks.clear();
}

ThreadContext& JobSystem::GetMainContext()
{
	return ThreadContexts[MAIN_THREAD];
}

ThreadContext& JobSystem::GetRenderContext()
{
	return ThreadContexts[RENDER_THREAD];
}

ThreadContext& JobSystem::GetThreadContext(uint32_t threadIndex)
{
	ASSERT(threadIndex < ThreadContexts.size());
	return ThreadContexts[threadIndex];
}

uint32_t JobSystem::GetThreadCount() const noexcept
{
	return static_cast<uint32_t>(ThreadContexts.size()); // enkiTotal + 1
}

uint32_t JobSystem::GetWorkerCount() const noexcept
{
	const uint32_t total = static_cast<uint32_t>(ThreadContexts.size());
	return total > 2u ? total - 2u : 0u;
}
