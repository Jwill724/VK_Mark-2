#pragma once

#include "common/Vk_Types.h"
#include "common/ResourceTypes.h"

struct QueueFamilyIndices {
	std::optional<uint32_t> graphicsFamily;
	std::optional<uint32_t> presentFamily;
	std::optional<uint32_t> transferFamily;
	std::optional<uint32_t> computeFamily;

	bool isComplete() const {
		return graphicsFamily.has_value() && presentFamily.has_value() && transferFamily.has_value() && computeFamily.has_value();
	}
};

namespace VulkanUtils {
	uint32_t FindMemoryType(VkPhysicalDevice pDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);
	QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice pDevice, VkSurfaceKHR surface);

	VkFormat findDepthFormat(VkPhysicalDevice pDevice);
	VkFormat findSupportedFormat(VkPhysicalDevice pDevice, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags feature);
	bool hasStencilComponent(VkFormat format);
	bool loadShaderModule(const char* filePath, VkDevice device, VkShaderModule* outShaderModule);
	VmaAllocator createAllocator(VkPhysicalDevice pDevice, VkDevice device, VkInstance instance);

	VkDeviceAddress getBufferAddress(VkBuffer buffer, VkDevice device);

	// Vulkan device limits
	std::vector<uint32_t> findSupportedSampleCounts(VkPhysicalDeviceLimits deviceLimits);

	void defineViewportAndScissor(VkCommandBuffer cmd, VkExtent2D drawExtent);
}