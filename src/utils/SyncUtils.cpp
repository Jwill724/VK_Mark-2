#include "pch.h"

#include "SyncUtils.h"

VkSemaphore SyncUtils::createSemaphore(const VkDevice device) {
	static std::mutex mutex;
	std::scoped_lock lock(mutex);

	VkSemaphoreCreateInfo semaphoreInfo{};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkSemaphore semaphore;
	VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore));

	return semaphore;
}

void SyncUtils::createTimelineSemaphore(TimelineSync& sync, const VkDevice device) {
	static std::mutex mutex;
	std::scoped_lock lock(mutex);
	VkSemaphoreTypeCreateInfo timelineCreateInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
		.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
		.initialValue = 0
	};

	VkSemaphoreCreateInfo createInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = &timelineCreateInfo
	};

	VK_CHECK(vkCreateSemaphore(device, &createInfo, nullptr, &sync.semaphore));
	sync.signalValue = 1;
}

VkFence SyncUtils::createFence(const VkDevice device) {
	static std::mutex mutex;
	std::scoped_lock lock(mutex);

	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	VkFence fence;
	VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &fence));

	return fence;
}