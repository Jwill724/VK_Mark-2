#pragma once

#include "ResourceTypes.h"
#include "EngineConstants.h"
#include "ErrorChecking.h"
#include "Vk_Types.h"

// Device getters are required for this to work, call into an auto
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

enum EngineStage : uint32_t {
	ENGINE_STAGE_NONE = 0, // No dependencies

	// Asset loading
	ENGINE_STAGE_LOADING_START = 1 << 1,
	ENGINE_STAGE_LOADING_FILES_READY = 1 << 2,
	ENGINE_STAGE_LOADING_SAMPLERS_READY = 1 << 3,
	ENGINE_STAGE_LOADING_TEXTURES_READY = 1 << 4,
	ENGINE_STAGE_LOADING_MATERIALS_READY = 1 << 5,
	ENGINE_STAGE_LOADING_MESHES_READY = 1 << 6,
	ENGINE_STAGE_LOADING_UPLOAD_MESH_DATA = 1 << 7,
	ENGINE_STAGE_LOADING_SCENE_GRAPH_READY = 1 << 8,


	// === Render stages ===
	ENGINE_STAGE_RENDER_PREPARING_FRAME = 1 << 9,
	ENGINE_STAGE_RENDER_FRAME_CONTEXT_READY = 1 << 10,
	ENGINE_STAGE_RENDER_CAMERA_READY = 1 << 11,
	ENGINE_STAGE_RENDER_FRUSTUM_READY = 1 << 12,
	ENGINE_STAGE_RENDER_SCENE_READY = 1 << 13,
	ENGINE_STAGE_RENDER_READY_TO_RENDER = 1 << 14,
	ENGINE_STAGE_RENDER_FRAME_IN_FLIGHT = 1 << 15,

	// === Global usages ===
	ENGINE_STAGE_READY = 1 << 16,
	ENGINE_STAGE_SHUTDOWN = 1 << 17,
	ENGINE_STAGE_SHUTDOWN_COMPLETE = 1 << 18
};

enum class GLTFJobType {
	DecodeImages,
	BuildSamplers,
	ProcessMaterials,
	ProcessMeshes,
	MeshBufferReady
};

enum class AddressBufferType {
	Instance,
	IndirectCmd,
	DrawRange,
	Material
};

enum class QueueType {
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
	AllocatedBuffer stagingBuffer{}; // Always free this right after use of it
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

	inline void destroy() {
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

	QueueType qType;

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

	inline VkFence submitWithSyncTimeline(
		const std::vector<VkCommandBuffer>& cmdBuffers,
		VkSemaphore timelineSemaphore,
		uint64_t signalValue,
		VkFence externalFence = VK_NULL_HANDLE
	) {
		std::scoped_lock lock(submitMutex);

		VkFence fence = externalFence ? externalFence : fencePool.get();

		std::vector<VkCommandBufferSubmitInfo> cmdInfos;
		cmdInfos.reserve(cmdBuffers.size());

		for (const auto& cmd : cmdBuffers) {
			VkCommandBufferSubmitInfo cmdInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
			cmdInfo.commandBuffer = cmd;
			cmdInfo.deviceMask = 0;
			cmdInfos.push_back(cmdInfo);
		}

		// === Timeline Semaphore Signal ===
		VkSemaphoreSubmitInfo signal{};
		signal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		signal.semaphore = timelineSemaphore;
		signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT; // conservative
		signal.deviceIndex = 0;
		signal.value = signalValue;

		VkSubmitInfo2 submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
		submitInfo.commandBufferInfoCount = static_cast<uint32_t>(cmdInfos.size());
		submitInfo.pCommandBufferInfos = cmdInfos.data();
		submitInfo.signalSemaphoreInfoCount = 1;
		submitInfo.pSignalSemaphoreInfos = &signal;

		VK_CHECK(vkQueueSubmit2(queue, 1, &submitInfo, fence));

		return externalFence ? VK_NULL_HANDLE : fence;
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

// Rendering structs
struct RenderObject {
	uint32_t instanceIndex;
	uint32_t drawRangeIndex;
	uint32_t materialIndex;
	MaterialPass passType;

	AABB aabb;
	glm::mat4 modelMatrix;
};

struct SortedBatch {
	GraphicsPipeline* pipeline;
	std::vector<IndirectDrawCmd> cmds;
	uint32_t drawOffset;
};

struct DrawContext {
	std::vector<RenderObject> OpaqueSurfaces;
	std::vector<RenderObject> TransparentSurfaces;
	Frustum frustum;
};