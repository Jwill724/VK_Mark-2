#include "pch.h"

#include "JobSystem.h"
#include "common/Vk_Types.h"
#include "EngineState.h"
#include "common/EngineConstants.h"
#include "vulkan/Backend.h"

static enki::TaskScheduler scheduler;
static std::vector<ThreadContext> threadContexts;
std::vector<ThreadContext>& getAllThreadContexts() {
	return threadContexts;
}

static ThreadCommandPoolManager _threadPoolManager;
ThreadCommandPoolManager& JobSystem::getThreadPoolManager() {
	return _threadPoolManager;
}

class StagedTask : public enki::ITaskSet {
public:
	StagedTask(std::function<void(ThreadContext&)> fn)
		: ITaskSet(1), _fn(std::move(fn)) {
	}

	void ExecuteRange(enki::TaskSetPartition, uint32_t threadIndex) override {
		ThreadContext& ctx = threadContexts[threadIndex];
		fmt::print("[JobSystem] Running job on thread {}\n", ctx.threadID);
		_fn(ctx);
	}

private:
	std::function<void(ThreadContext&)> _fn;
};

void JobSystem::initScheduler() {
	enki::TaskSchedulerConfig config;
	config.numTaskThreadsToCreate = std::thread::hardware_concurrency() - 1; // Need one thread for main
	scheduler.Initialize(config);

	const uint32_t numEnkiThreads = scheduler.GetNumTaskThreads();
	fmt::print("[JobSystem] Enki initialized with {} task threads\n", numEnkiThreads);

	threadContexts.resize(numEnkiThreads);

	for (uint32_t i = 0; i < numEnkiThreads; ++i) {
		auto& ctx = threadContexts[i];
		ctx.threadID = i;
	}
}

void JobSystem::shutdownScheduler() {
	scheduler.WaitforAllAndShutdown();
}

void JobSystem::submitJob(std::function<void(ThreadContext&)> taskFn) {
	auto* task = new StagedTask(std::move(taskFn));
	scheduler.AddTaskSetToPipe(task);
}

void JobSystem::wait() {
	scheduler.WaitforAll();
}