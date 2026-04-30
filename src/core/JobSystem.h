#pragma once

#include "core/Logging.h"
#include "functional"

struct ThreadContext;

class Engine;

class JobSystem final
{
	friend class Engine;
public:
	void SubmitJob(std::function<void(ThreadContext&)> taskFn);
	void Wait();

	uint32_t GetThreadCount() const noexcept;

	Logging Logger;
private:
	void InitScheduler();
	void ShutdownScheduler();
};
