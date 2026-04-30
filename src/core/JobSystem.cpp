#include "pch.h"

#include "JobSystem.h"
#include "EngineTypes.h"

static enki::TaskScheduler Scheduler;
static std::vector<ThreadContext> ThreadContexts;

class StagedTask : public enki::ITaskSet
{
public:
	StagedTask(std::function<void(ThreadContext&)> fn)
		: ITaskSet(1),  m_fn(std::move(fn)) {
	}

	void ExecuteRange(enki::TaskSetPartition, uint32_t threadIndex) override
	{
		ThreadContext& ctx = ThreadContexts[threadIndex];
		fmt::println("[JobSystem] Running job on thread {}", ctx.threadID);
		m_fn(ctx);
	}

private:
	std::function<void(ThreadContext&)> m_fn;
};

void JobSystem::InitScheduler()
{
	enki::TaskSchedulerConfig config;
	config.numTaskThreadsToCreate = std::thread::hardware_concurrency() - 1; // Need one thread for main
	Scheduler.Initialize(config);

	const uint32_t numEnkiThreads = Scheduler.GetNumTaskThreads();
	fmt::println("[JobSystem] Enki initialized with {} task threads", numEnkiThreads);

	ThreadContexts.resize(numEnkiThreads);

	for (uint32_t i = 0; i < numEnkiThreads; ++i)
	{
		auto& ctx = ThreadContexts[i];
		ctx.threadID = i;
	}
}

uint32_t JobSystem::GetThreadCount() const noexcept
{
	return static_cast<uint32_t>(ThreadContexts.size());
}

void JobSystem::ShutdownScheduler() { Scheduler.WaitforAllAndShutdown(); }

void JobSystem::SubmitJob(std::function<void(ThreadContext&)> taskFn)
{
	auto* task = new StagedTask(std::move(taskFn));
	Scheduler.AddTaskSetToPipe(task);
}

void JobSystem::Wait() { Scheduler.WaitforAll(); }
