#pragma once

#include "VulkanForward.h"
#include <vector>

struct PhysicalDeviceCandidate;

class PhysicalDeviceSelector
{
public:
	static PhysicalDeviceCandidate PickBest(
		VkInstance instance,
		VkSurfaceKHR surface,
		const std::vector<const char*> deviceExtensions
	);
};
