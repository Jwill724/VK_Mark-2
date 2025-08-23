#pragma once

#include "utils/VulkanUtils.h"
#include "common/EngineTypes.h"
#include "BackendTools.h"

namespace Backend {
	const VkPhysicalDeviceLimits getDeviceLimits();
	const size_t getNonCoherentAtomSize();

	VkInstance getInstance();
	VkSurfaceKHR getSurface();
	VkPhysicalDevice getPhysicalDevice();
	VkDevice getDevice();

	SwapchainDef& getSwapchainDef();

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