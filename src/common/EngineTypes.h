#pragma once

#include "Core.h"

// Very simple function fifo queue
// Primary use for storing destruction for buffers and images.
// Thanks vkguide
class DeletionQueue
{
public:
	void PushFunction(std::function<void()>&& function)
	{
		m_deletors.push_back(function);
	}

	void Flush()
	{
		if (m_deletors.empty()) return;

		// reverse iterate the deletion queue to execute all the functions
		for (auto it = m_deletors.rbegin(); it != m_deletors.rend(); ++it)
		{
			(*it)(); //call functors
		}

		m_deletors.clear();
	}
private:
	std::deque<std::function<void()>> m_deletors;
};


enum class GLTFJobType
{
	DecodeImages,
	BuildSamplers,
	ProcessMaterials,
	ProcessMeshes,
	Count
};

struct JobInfo
{
	std::function<void(uint32_t threadID)> task;
	uint32_t requiredStages;
	bool done = false;
};

struct BaseWorkQueue
{
	virtual ~BaseWorkQueue() = default;
};

template<typename T>
class DeferredWorkQueue
{
public:
	void Push(const T& workItem)
	{
		std::scoped_lock lock(m_mutex);
		m_queue.push_back(workItem);
	}

	std::vector<T> Collect()
	{
		std::scoped_lock lock(m_mutex);
		std::vector<T> result = std::move(m_queue);
		m_queue.clear();
		return result;
	}

	bool Empty() const
	{
		std::scoped_lock lock(m_mutex);
		return m_queue.empty();
	}

private:
	mutable std::mutex m_mutex;
	std::vector<T> m_queue;
};

template<typename T>
class TypedWorkQueue : BaseWorkQueue
{
	void Push(const T& item) { m_queue.Push(item); }
	std::vector<T> Collect() { return m_queue.Collect(); }
	bool Empty() const { return m_queue.Empty(); }

private:
	DeferredWorkQueue<T> m_queue;
};

struct LinearAllocator
{
	uint8_t* base;
	size_t   offset;
	size_t   capacity;
};

struct ThreadContext
{
	uint32_t threadID = 0;

	// Generic per-thread memory
	LinearAllocator scratchAllocator{};

	// Reusable temp storage
	std::vector<uint8_t> scratchBuffer{};

	// Job system
	BaseWorkQueue* workQueueActive = nullptr;

	// Debug / profiling
	uint32_t jobsExecuted = 0;

	// Optional utilities
	uint32_t randomState = 0;
};

// Thread and queue connector struct
struct ScopedWorkQueue
{
	ThreadContext& ctx;
	BaseWorkQueue* previousQueue;

	ScopedWorkQueue(ThreadContext& ctx, BaseWorkQueue* newQueue)
		: ctx(ctx), previousQueue(ctx.workQueueActive)
	{
		ctx.workQueueActive = newQueue;
	}

	~ScopedWorkQueue()
	{
		ctx.workQueueActive = previousQueue;
	}
};

struct ShadowControl
{
	float splitLambda          = 0.97f;
	float bias                 = 0.0001f;
	float softnessFactor;
	float maxCasterDistance[4] = { 3000.0f, 4000.0f, 5000.0f, 6000.0f };
	float xyPadding            = 150.0f;
	float lsEpsilon            = 5.0f;
	float dirEpsilon           = 20.0f;
	float shadowRadii[4]       = { 17.0f, 46.0f, 160.0f, 1000.0f };
};
