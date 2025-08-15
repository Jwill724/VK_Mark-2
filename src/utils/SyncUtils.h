#pragma once

#include "common/Vk_Types.h"

namespace SyncUtils {
	VkSemaphore createSemaphore(const VkDevice device);
	void createTimelineSemaphore(TimelineSync& sync, const VkDevice device);
	VkFence createFence(const VkDevice device);
}