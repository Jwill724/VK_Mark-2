#pragma once

#include "VulkanTypes.h"

class PhysicalDeviceSelector
{
public:
	static PhysicalDeviceCandidate PickBest(
		VkInstance instance,
		VkSurfaceKHR surface,
		const std::vector<const char*> deviceExtensions
	);
};
