#pragma once

#include "Window.h"
#include "BackendTools.h"
#include "utils/VulkanUtils.h"

namespace Backend {
	VkInstance& getInstance();
	VkPhysicalDevice& getPhysicalDevice();
	VkDevice& getDevice();
	VkSurfaceKHR& getSurface();

	VkSwapchainKHR& getSwapchain();
	VkExtent2D& getSwapchainExtent();
	std::vector<VkImageView> getSwapchainImageViews();
	std::vector<VkImage> getSwapchainImages();

	QueueFamilyIndices& getQueueFamilyIndices();

	VkQueue& getGraphicsQueue();
	VkQueue& getPresentQueue();
	VkQueue& getTransferQueue();

	VkFormat& getSwapchainImageFormat();

	// api setup
	void initVulkan();

	// allocates swapchains and any other resources
	void initBackend();

	void recreateSwapchain();

	// deletes in proper order
	void cleanupBackend();
}