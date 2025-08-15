#pragma once

#include "common/EngineTypes.h"

namespace CommandBuffer {
	VkCommandPool createCommandPool(VkDevice device, uint32_t queueFamilyIndex);
	VkCommandBuffer createCommandBuffer(VkDevice device, VkCommandPool commandPool);
	VkCommandBuffer createSecondaryCmd(VkDevice device, VkCommandPool pool, VkCommandBufferInheritanceInfo& inheritance);
	void recordDeferredCmd(std::function<void(VkCommandBuffer)>&& function, VkCommandPool cmdPool, QueueType type, VkDevice device);
};