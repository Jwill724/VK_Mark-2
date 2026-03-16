#pragma once

#include "utils/VulkanUtils.h"
#include "common/EngineTypes.h"
#include "BackendTools.h"

namespace Backend {
	const VkPhysicalDeviceLimits getDeviceLimits();
	const size_t getNonCoherentAtomSize();
	const float getTimestampPeriod();
	bool queueSupportsTimestamps(const GPUQueue& queue);

	VkInstance getInstance();
	VkSurfaceKHR getSurface();
	VkPhysicalDevice getPhysicalDevice();
	VkDevice getDevice();

	const std::string getDeviceName();

	SwapchainDef& getSwapchainDef();

	const bool isComputeAvailable();
	//const bool isTransferAvailable();

	QueueFamilyIndices getQueueFamilyIndices();
	GPUQueue& getGraphicsQueue();
	GPUQueue& getPresentQueue();
	GPUQueue& getTransferQueue();
	GPUQueue& getComputeQueue();

	void initVulkanCore();

	const void deviceIdle();

	void resizeSwapchain();

	void cleanupBackend();
}
