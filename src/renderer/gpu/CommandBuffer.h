#pragma once

#include "common/Vk_Types.h"
#include "common/EngineConstants.h"
#include "common/EngineTypes.h"

namespace CommandBuffer {
	VkCommandPool createCommandPool(VkDevice device, uint32_t queueFamilyIndex);
	VkCommandBuffer createCommandBuffer(VkDevice device, VkCommandPool commandPool);
	void recordDeferredCmd(std::function<void(VkCommandBuffer)>&& function, VkCommandPool cmdPool, bool transferUse = false);
};