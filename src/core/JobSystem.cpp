#include "pch.h"

#include "JobSystem.h"
#include "EngineTypes.h"

static enki::TaskScheduler Scheduler;
static std::vector<ThreadContext> ThreadContexts;

static std::mutex s_pendingMutex;
static std::vector<enki::ICompletable*> s_pendingTasks;

class WorkerTask : public enki::ITaskSet
{
public:
	WorkerTask(std::function<void(ThreadContext&)> fn)
		: ITaskSet(1), m_fn(std::move(fn)) {}

	void ExecuteRange(enki::TaskSetPartition, uint32_t threadIndex) override
	{
		const uint32_t ourIdx = (threadIndex == 0u)
			? JobSystem::MAIN_THREAD
			: threadIndex + 1u;

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
	}

private:
	std::function<void(ThreadContext&)> m_fn;
};

namespace
{
	class ScopedRangeTask final : public enki::ITaskSet
	{
	public:
		const std::function<void(ThreadContext&, uint32_t)>* m_fn = nullptr;

		void ExecuteRange(enki::TaskSetPartition range, uint32_t threadIndex) override
		{
			const uint32_t ourIdx = (threadIndex == 0u)
				? JobSystem::MAIN_THREAD
				: threadIndex + 1u;

			ThreadContext& ctx = ThreadContexts[ourIdx];

			for (uint32_t i = range.start; i < range.end; ++i)
			{
				ctx.jobsExecuted++;
				(*m_fn)(ctx, i);
			}
		}
	};
}

void JobSystem::RunParallel(
	uint32_t count,
	const std::function<void(ThreadContext&, uint32_t)>& fn)
{
	if (count == 0u) return;

	if (count == 1u)
	{
		const uint32_t enkiIdx = Scheduler.GetThreadNum();
		const uint32_t ourIdx  = (enkiIdx == 0u) ? MAIN_THREAD : enkiIdx + 1u;

		ThreadContext& ctx = ThreadContexts[ourIdx];
		ctx.jobsExecuted++;
		fn(ctx, 0u);
		return;
	}

	ScopedRangeTask task;
	task.m_fn      = &fn;
	task.m_SetSize = count;

	task.m_MinRange = 1u;

	Scheduler.AddTaskSetToPipe(&task);

	Scheduler.WaitforTask(&task);
}

void JobSystem::InitScheduler()
{
	enki::TaskSchedulerConfig config;

	const uint32_t hw = std::thread::hardware_concurrency();
	const uint32_t enkiWorkers = (hw > 2u) ? hw - 2u : 1u;
	config.numTaskThreadsToCreate = enkiWorkers;
	Scheduler.Initialize(config);

	const uint32_t enkiTotal = Scheduler.GetNumTaskThreads();

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
		const uint32_t ourIdx = i + 1;
		auto& ctx      = ThreadContexts[ourIdx];
		ctx.threadID   = ourIdx;
		ctx.threadRole = ThreadRole::Worker;
	}

	Logger.Print("[JobSystem] Initialized: 1 main, 1 render, {} workers",
		GetWorkerCount());
}

void JobSystem::ShutdownScheduler()
{
	Scheduler.WaitforAllAndShutdown();
}

void JobSystem::SubmitJob(std::function<void(ThreadContext&)> taskFn)
{
	auto* task = new WorkerTask(std::move(taskFn));
	{
		std::scoped_lock lk(s_pendingMutex);
		s_pendingTasks.push_back(task);
	}
	Scheduler.AddTaskSetToPipe(task);
}

void JobSystem::SubmitRenderJob(std::function<void(ThreadContext&)> taskFn)
{
	auto* task = new PinnedTask(std::move(taskFn), RENDER_THREAD);
	{
		std::scoped_lock lk(s_pendingMutex);
		s_pendingTasks.push_back(task);
	}
	Scheduler.AddPinnedTask(task);
}

void JobSystem::SubmitMainJob(std::function<void(ThreadContext&)> taskFn)
{
	auto* task = new PinnedTask(std::move(taskFn), MAIN_THREAD);
	{
		std::scoped_lock lk(s_pendingMutex);
		s_pendingTasks.push_back(task);
	}
	Scheduler.AddPinnedTask(task);
}

void JobSystem::PumpMainThread()   { Scheduler.RunPinnedTasks(); }
void JobSystem::PumpRenderThread() { Scheduler.RunPinnedTasks(); }

void JobSystem::Wait()
{
	Scheduler.WaitforAll();

	std::vector<enki::ICompletable*> toFree;
	{
		std::scoped_lock lk(s_pendingMutex);
		toFree.swap(s_pendingTasks);
	}

	for (auto* t : toFree)
		delete t;
}

ThreadContext& JobSystem::GetMainContext()   { return ThreadContexts[MAIN_THREAD]; }
ThreadContext& JobSystem::GetRenderContext() { return ThreadContexts[RENDER_THREAD]; }

ThreadContext& JobSystem::GetThreadContext(uint32_t threadIndex)
{
	ASSERT(threadIndex < ThreadContexts.size());
	return ThreadContexts[threadIndex];
}

uint32_t JobSystem::GetThreadCount() const noexcept
{
	return static_cast<uint32_t>(ThreadContexts.size());
}

uint32_t JobSystem::GetWorkerCount() const noexcept
{
	const uint32_t total = static_cast<uint32_t>(ThreadContexts.size());
	return total > 2u ? total - 2u : 0u;
}
