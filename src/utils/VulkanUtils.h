#pragma once

#include "common/Vk_Types.h"

struct QueueFamilyIndices {
	std::optional<uint32_t> graphicsFamily;
	std::optional<uint32_t> presentFamily;
	std::optional<uint32_t> transferFamily;

	bool isComplete() const {
		return graphicsFamily.has_value() && presentFamily.has_value() && transferFamily.has_value();
	}
};

namespace VulkanUtils {
	uint32_t FindMemoryType(VkPhysicalDevice device, uint32_t typeFilter, VkMemoryPropertyFlags properties);
	QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);

	VkFormat findDepthFormat(VkPhysicalDevice device);
	VkFormat findSupportedFormat(VkPhysicalDevice device, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags feature);
	bool hasStencilComponent(VkFormat format);
	bool loadShaderModule(const char* filePath, VkDevice device, VkShaderModule* outShaderModule);
	VmaAllocator createAllocator(VkPhysicalDevice physicalDevice, VkDevice device, VkInstance instance);

	// Push constants
	uint32_t GetMaxPushConstantSize(VkPhysicalDevice device);
	PushConstantPool CreatePushConstantPool(VkPhysicalDevice device);
	bool AllocatePushConstant(PushConstantPool* pool, uint32_t size, uint32_t* offsetOut);
}