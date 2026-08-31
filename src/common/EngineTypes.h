#pragma once

#include <vector>
#include <mutex>
#include <functional>
#include <deque>
#include <array>
#include <cstdint>

#ifndef NDEBUG
#include <cassert>
#define ASSERT(x) assert(x)
#else
#define ASSERT(x) ((void)0)
#endif

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

struct Extents2D
{
	std::array<uint32_t, 2> data { 0, 0 };

	uint32_t& Width()        { return data[0]; }
	uint32_t  Width()  const { return data[0]; }
	uint32_t& Height()       { return data[1]; }
	uint32_t  Height() const { return data[1]; }

	bool IsDefined() const noexcept
	{
		return (data[0] != 0 && data[1] != 0);
	}
};

struct Extents3D
{
	std::array<uint32_t, 3> data { 0, 0, 0 };

	uint32_t& Width()         { return data[0]; }
	uint32_t  Width()  const  { return data[0]; }
	uint32_t& Height()        { return data[1]; }
	uint32_t  Height() const  { return data[1]; }
	uint32_t& Depth()         { return data[2]; }
	uint32_t  Depth()  const  { return data[2]; }

	bool IsDefined() const noexcept
	{
		return (data[0] != 0 && data[1] != 0 && data[2] != 0);
	}
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
public:
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

enum class ThreadRole
{
	Main,
	Render,
	Worker
};

enum class AssetJobType
{
	LoadFile,
	DecodeImages,
	BuildSamplers,
	ProcessMaterials,
	ProcessMeshes,
	BuildSceneGraph,
	Count
};

struct ThreadScratch
{
	std::vector<uint8_t> bufferA; // vertex scratch — sized as needed
	std::vector<uint8_t> bufferB; // index scratch
	std::vector<uint8_t> bufferC; // lod index scratch

	void Reset()
	{
		bufferA.clear();
		bufferB.clear();
		bufferC.clear();
	}
};

struct ThreadContext
{
	uint32_t   threadID   = 0;
	ThreadRole threadRole = ThreadRole::Worker;

	LinearAllocator      scratchAllocator{};
	std::vector<uint8_t> scratchBuffer{};
	BaseWorkQueue*       workQueueActive = nullptr;
	ThreadScratch        scratch{};        // type-erased, cast at use site
	uint32_t             jobsExecuted    = 0;
	uint32_t             randomState     = 0;
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
