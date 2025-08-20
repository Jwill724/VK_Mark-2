#pragma once

#include "common/EngineTypes.h"
#include "renderer/gpu/CommandBuffer.h"

struct ThreadContext;

std::vector<ThreadContext>& getAllThreadContexts();

struct ThreadCommandPool {
	VkCommandPool graphicsPool = VK_NULL_HANDLE;
	VkCommandPool transferPool = VK_NULL_HANDLE;

	ThreadCommandPool() = default;

	// Disallow copy
	ThreadCommandPool(const ThreadCommandPool&) = delete;
	ThreadCommandPool& operator=(const ThreadCommandPool&) = delete;

	// Allow move
	ThreadCommandPool(ThreadCommandPool&&) = default;
	ThreadCommandPool& operator=(ThreadCommandPool&&) = default;
};

struct ThreadCommandPoolManager {
	std::vector<ThreadCommandPool> perThreadPools;

	inline void init(VkDevice device, uint32_t threadCount, uint32_t graphicsFamily, uint32_t transferFamily) {
		fmt::print("[ThreadCommandPoolManager] Initializing command pools for {} threads\n", threadCount);
		perThreadPools.resize(threadCount);
		for (uint32_t i = 0; i < threadCount; ++i) {
			auto& pool = perThreadPools[i];
			pool.graphicsPool = CommandBuffer::createCommandPool(device, graphicsFamily);
			pool.transferPool = CommandBuffer::createCommandPool(device, transferFamily);

			fmt::print("  [Thread {}] GraphicsPool = {}, TransferPool = {}\n",
				i,
				static_cast<void*>(pool.graphicsPool),
				static_cast<void*>(pool.transferPool));
		}
	}

	inline void cleanup(VkDevice device) {
		fmt::print("[ThreadCommandPoolManager] Cleaning up command pools\n");
		for (uint32_t i = 0; i < perThreadPools.size(); ++i) {
			auto& pool = perThreadPools[i];

			if (pool.graphicsPool) {
				fmt::print("  [Thread {}] Destroying GraphicsPool = {}\n", i, static_cast<void*>(pool.graphicsPool));
				vkDestroyCommandPool(device, pool.graphicsPool, nullptr);
			}

			if (pool.transferPool) {
				fmt::print("  [Thread {}] Destroying TransferPool = {}\n", i, static_cast<void*>(pool.transferPool));
				vkDestroyCommandPool(device, pool.transferPool, nullptr);
			}
		}
	}

	inline VkCommandPool getPool(uint32_t threadID, QueueType type) {
		VkCommandPool selected = (type == QueueType::Graphics)
			? perThreadPools[threadID].graphicsPool
			: perThreadPools[threadID].transferPool;

		fmt::print("  [ThreadCommandPoolManager] getPool(Thread {}, Type: {}) -> {}\n",
			threadID,
			(type == QueueType::Graphics ? "Graphics" : "Transfer"),
			static_cast<void*>(selected));

		return selected;
	}
};


namespace JobSystem {
	struct LogMessage {
		uint32_t threadID;
		std::string text;
	};

	inline std::vector<LogMessage> logMessages;
	inline std::mutex logMutex;

	// Log output from parallel jobs is not strictly ordered,
	// messages may appear earlier or later depending on workload size.
	inline void log(uint32_t threadID, const std::string& text) {
		std::scoped_lock lock(logMutex);
		logMessages.push_back({ threadID, text });
	}

	inline void flushLogs() {
		std::scoped_lock lock(logMutex);
		for (auto& msg : logMessages) {
			fmt::print("[Thread {}] {}", msg.threadID, msg.text);
		}
		logMessages.clear();
	}

	void initScheduler();
	void shutdownScheduler();
	void submitJob(std::function<void(ThreadContext&)> taskFn);
	void wait();

	ThreadCommandPoolManager& getThreadPoolManager();
}