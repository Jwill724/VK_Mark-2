#pragma once

#include "Window.h"
#include "BackendTools.h"
#include "utils/VulkanUtils.h"

namespace Backend {
	VkInstance& getInstance();
	VkSurfaceKHR& getSurface();
	VkPhysicalDevice& getPhysicalDevice();
	VkDevice& getDevice();

	// TODO: swapchain state struct
	VkSwapchainKHR& getSwapchain();
	VkExtent2D& getSwapchainExtent();
	std::vector<VkImageView>& getSwapchainImageViews();
	std::vector<VkImage>& getSwapchainImages();
	VkFormat& getSwapchainImageFormat();

	QueueFamilyIndices& getQueueFamilyIndices();

	VkQueue& getGraphicsQueue();
	VkQueue& getPresentQueue();
	VkQueue& getTransferQueue();

	// api setup
	// queues and devices
	void initVulkan();

	// allocates swapchains and any other resources
	void initBackend();

	void resizeSwapchain();

	// deletes in proper order
	void cleanupBackend();
}