#pragma once

#include "Vk_Types.h"
#include "ErrorChecking.h"

// General use cpu side
// Device getters are required for this to work, call into an auto
// thanks vkguide
struct DeletionQueue {
	std::deque<std::function<void()>> deletors;

	void push_function(std::function<void()>&& function) {
		deletors.push_back(function);
	}

	void flush() {
		// reverse iterate the deletion queue to execute all the functions
		for (auto it = deletors.rbegin(); it != deletors.rend(); ++it) {
			(*it)(); //call functors
		}

		deletors.clear();
	}
};

struct TimelineDeletionQueue {
	struct Entry {
		uint64_t timelineValue = UINT32_MAX;
		std::function<void()> destroy;
	};

	VkSemaphore semaphore = VK_NULL_HANDLE;
	std::vector<Entry> queue;

	inline void enqueue(uint64_t timelineValue, std::function<void()> fn) {
		queue.push_back({ timelineValue, std::move(fn) });
	}

	inline void process(VkDevice device) {
		if (queue.empty()) return;
		uint64_t current = 0;
		VK_CHECK(vkGetSemaphoreCounterValue(device, semaphore, &current));

		auto it = queue.begin();
		while (it != queue.end()) {
			if (current >= it->timelineValue) {
				it->destroy();
				it = queue.erase(it);
			}
			else {
				++it;
			}
		}
	}
};

enum EngineStage : uint32_t {
	ENGINE_STAGE_NONE = 0, // No dependencies

	// Asset loading
	ENGINE_STAGE_LOADING_START = 1 << 1,
	ENGINE_STAGE_LOADING_FILES_READY = 1 << 2,
	ENGINE_STAGE_LOADING_SAMPLERS_READY = 1 << 3,
	ENGINE_STAGE_LOADING_TEXTURES_READY = 1 << 4,
	ENGINE_STAGE_LOADING_MATERIALS_READY = 1 << 5,
	ENGINE_STAGE_LOADING_MESHES_READY = 1 << 6,
	ENGINE_STAGE_LOADING_SCENE_GRAPH_READY = 1 << 7,


	// === Render stages ===
	ENGINE_STAGE_RENDER_PREPARING_FRAME = 1 << 8,
	ENGINE_STAGE_RENDER_FRAME_CONTEXT_READY = 1 << 9,
	ENGINE_STAGE_RENDER_CAMERA_READY = 1 << 10,
	ENGINE_STAGE_RENDER_FRUSTUM_READY = 1 << 11,
	ENGINE_STAGE_RENDER_SCENE_READY = 1 << 12,
	ENGINE_STAGE_RENDER_READY_TO_RENDER = 1 << 13,
	ENGINE_STAGE_RENDER_FRAME_IN_FLIGHT = 1 << 14,

	// === Global usages ===
	ENGINE_STAGE_READY = 1 << 15,
	ENGINE_STAGE_SHUTDOWN = 1 << 16,
	ENGINE_STAGE_SHUTDOWN_COMPLETE = 1 << 17
};

enum class GLTFJobType {
	DecodeImages,
	BuildSamplers,
	ProcessMaterials,
	ProcessMeshes
};

enum class QueueType : uint8_t {
	Graphics,
	Transfer,
	Present,
	Compute,
	Generic
};

struct JobInfo {
	std::function<void(uint32_t threadID)> task;
	uint32_t requiredStages;
	QueueType queueType;
	bool done = false;
};

struct BaseWorkQueue {
	virtual ~BaseWorkQueue() = default;
};

template<typename T>
class DeferredWorkQueue {
public:
	void push(const T& workItem) {
		std::scoped_lock lock(_mutex);
		_queue.push_back(workItem);
	}

	std::vector<T> collect() {
		std::scoped_lock lock(_mutex);
		std::vector<T> result = std::move(_queue);
		_queue.clear();
		return result;
	}

	bool empty() const {
		std::scoped_lock lock(_mutex);
		return _queue.empty();
	}

private:
	mutable std::mutex _mutex;
	std::vector<T> _queue;
};

template<typename T>
struct TypedWorkQueue : BaseWorkQueue {
	DeferredWorkQueue<T> queue;

	void push(const T& item) { queue.push(item); }
	std::vector<T> collect() { return queue.collect(); }
	bool empty() const { return queue.empty(); }
};

struct ThreadContext {
	int threadID = 0;
	QueueType queueType = QueueType::Generic;
	VkCommandPool cmdPool = VK_NULL_HANDLE;
	DeletionQueue deletionQueue{};
	void* stagingMapped = nullptr;
	BaseWorkQueue* workQueueActive = nullptr; // Functions handle this variables scope
	VkFence lastSubmittedFence = VK_NULL_HANDLE;
};

// Thread and queue connector struct
struct ScopedWorkQueue {
	ThreadContext& ctx;
	BaseWorkQueue* previousQueue;

	ScopedWorkQueue(ThreadContext& ctx, BaseWorkQueue* newQueue)
		: ctx(ctx), previousQueue(ctx.workQueueActive) {
		ctx.workQueueActive = newQueue;
	}

	~ScopedWorkQueue() {
		ctx.workQueueActive = previousQueue;
	}
};

struct FencePool {
	std::vector<VkFence> availableFences;
	std::vector<VkFence> inFlightFences;
	std::mutex mutex;

	VkDevice device = VK_NULL_HANDLE;

	inline VkFence get() {
		std::scoped_lock lock(mutex);
		if (!availableFences.empty()) {
			VkFence fence = availableFences.back();
			availableFences.pop_back();
			VK_CHECK(vkResetFences(device, 1, &fence));
			inFlightFences.push_back(fence);
			return fence;
		}

		VkFenceCreateInfo info{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		VkFence fence;
		VK_CHECK(vkCreateFence(device, &info, nullptr, &fence));
		inFlightFences.push_back(fence);
		return fence;
	}

	inline void recycle(VkFence fence) {
		std::scoped_lock lock(mutex);
		availableFences.push_back(fence);
		inFlightFences.erase(std::remove(inFlightFences.begin(), inFlightFences.end(), fence), inFlightFences.end());
	}

	inline bool isFenceReady(VkFence fence) const {
		return vkGetFenceStatus(device, fence) == VK_SUCCESS;
	}

	void resetAll() {
		std::scoped_lock lock(mutex);
		for (auto& fence : inFlightFences) {
			VK_CHECK(vkResetFences(device, 1, &fence));
			availableFences.push_back(fence);
		}
		inFlightFences.clear();
	}

	inline void destroyFences() {
		std::scoped_lock lock(mutex);
		for (auto& fence : availableFences)
			vkDestroyFence(device, fence, nullptr);
		for (auto& fence : inFlightFences)
			vkDestroyFence(device, fence, nullptr);
		availableFences.clear();
		inFlightFences.clear();
	}
};

struct GPUQueue {
	VkQueue queue = VK_NULL_HANDLE;
	std::mutex submitMutex;
	FencePool fencePool;

	uint32_t familyIndex = 0;

	// when a timelinesubmit is done, set to true
	// on upcoming queue uses check bool to see if a wait is needed or not
	std::atomic<bool> wasUsed = false;

	QueueType qType = QueueType::Generic;

	inline VkFence submit(const VkSubmitInfo& info) {
		std::scoped_lock lock(submitMutex);
		VkFence fence = fencePool.get();
		VK_CHECK(vkQueueSubmit(queue, 1, &info, fence));
		return fence;
	}

	inline VkFence submitBatch(const std::vector<VkSubmitInfo>& infos) {
		std::scoped_lock lock(submitMutex);
		VkFence fence = fencePool.get();
		VK_CHECK(vkQueueSubmit(queue, static_cast<uint32_t>(infos.size()), infos.data(), fence));
		return fence;
	}

	inline void waitIdle() {
		std::scoped_lock lock(submitMutex);
		VK_CHECK(vkQueueWaitIdle(queue));
	}

	inline void submitWithTimelineSync(
		const std::vector<VkCommandBuffer>& cmdBuffers,
		VkSemaphore timelineSemaphore,
		uint64_t signalValue,
		VkSemaphore waitSemaphore = VK_NULL_HANDLE,
		uint64_t waitValue = 0,
		bool waitUpAhead = false
	) {
		std::scoped_lock lock(submitMutex);

		std::vector<VkSemaphoreSubmitInfo> waitInfos;
		if (waitSemaphore != VK_NULL_HANDLE) {
			VkSemaphoreSubmitInfo waitInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
			waitInfo.semaphore = waitSemaphore;
			waitInfo.value = waitValue;
			waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			waitInfo.deviceIndex = 0;
			waitInfos.push_back(waitInfo);
		}

		std::vector<VkCommandBufferSubmitInfo> cmdInfos;
		cmdInfos.reserve(cmdBuffers.size());

		for (const auto& cmd : cmdBuffers) {
			VkCommandBufferSubmitInfo cmdInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
			cmdInfo.commandBuffer = cmd;
			cmdInfo.deviceMask = 0;
			cmdInfos.push_back(cmdInfo);
		}

		VkSemaphoreSubmitInfo signalInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
		signalInfo.semaphore = timelineSemaphore;
		signalInfo.value = signalValue;
		signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		signalInfo.deviceIndex = 0;

		VkSubmitInfo2 submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
		submitInfo.commandBufferInfoCount = static_cast<uint32_t>(cmdInfos.size());
		submitInfo.pCommandBufferInfos = cmdInfos.data();
		submitInfo.signalSemaphoreInfoCount = 1;
		submitInfo.pSignalSemaphoreInfos = &signalInfo;
		if (!waitInfos.empty()) {
			submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitInfos.size());
			submitInfo.pWaitSemaphoreInfos = waitInfos.data();
		}

		VK_CHECK(vkQueueSubmit2(queue, 1, &submitInfo, VK_NULL_HANDLE));

		if (waitUpAhead)
			wasUsed = true;
	}

	inline void waitTimelineValue(VkDevice device, VkSemaphore timelineSemaphore, uint64_t waitValue) {
		std::scoped_lock lock(submitMutex);

		VkSemaphoreWaitInfo waitInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
		waitInfo.flags = 0;
		waitInfo.semaphoreCount = 1;
		waitInfo.pSemaphores = &timelineSemaphore;
		waitInfo.pValues = &waitValue;

		VK_CHECK(vkWaitSemaphores(device, &waitInfo, UINT64_MAX));
	}
};

inline void waitAndRecycleLastFence(VkFence& lastSubmittedFence, GPUQueue& queue, VkDevice device) {
	if (lastSubmittedFence != VK_NULL_HANDLE) {
		if (!queue.fencePool.isFenceReady(lastSubmittedFence)) {
			VK_CHECK(vkWaitForFences(device, 1, &lastSubmittedFence, VK_TRUE, UINT64_MAX));
		}

		queue.fencePool.recycle(lastSubmittedFence);
		lastSubmittedFence = VK_NULL_HANDLE;
	}
	else {
		fmt::print("No fence... skipping.\n");
	}
}