#include "pch.h"

#include "VulkanUtils.h"

uint32_t VulkanUtils::FindMemoryType(VkPhysicalDevice device, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(device, &memProperties);

	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
		if (typeFilter & (1 << i) &&
			(memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}
	throw std::runtime_error("Failed to find suitable memory type!");
}

// TODO: queue manager system for active and inactive queues
QueueFamilyIndices VulkanUtils::FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
	QueueFamilyIndices indices;

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

	int i = 0;
	for (const auto& queueFamily : queueFamilies) {
		if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			indices.graphicsFamily = i;
		}

		VkBool32 presentSupport = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
		if (presentSupport) {
			indices.presentFamily = i;
		}

		// Find dedicated transfer queue
		if ((queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) && !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) && !indices.transferFamily.has_value()) {
			indices.transferFamily = i;
		}

		if (indices.isComplete()) {
			break;
		}

		i++;
	}

	if (!indices.transferFamily.has_value() && indices.graphicsFamily.has_value()) {
		std::cout << "No dedicated transfer queue, falling back to graphics queue." << std::endl;
		indices.transferFamily = indices.graphicsFamily;
	}

	return indices;
}

VkFormat VulkanUtils::findDepthFormat(VkPhysicalDevice device) {
	return findSupportedFormat(
		device,
		{ VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT },
		VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
	);
}

VkFormat VulkanUtils::findSupportedFormat(VkPhysicalDevice device, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags feature) {
	for (VkFormat format : candidates) {
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(device, format, &props);

		if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & feature) == feature) {
			return format;
		}
		else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & feature) == feature) {
			return format;
		}
	}

	throw std::runtime_error("Failed to find supported format!");
}

// TODO: more stencil options
bool VulkanUtils::hasStencilComponent(VkFormat format) {
	return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

bool VulkanUtils::loadShaderModule(const char* filePath, VkDevice device, VkShaderModule* outShaderModule) {
	// open the file. With cursor at the end
	std::ifstream file(filePath, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		return false;
	}

	// find what the size of the file is by looking up the location of the cursor
	// because the cursor is at the end, it gives the size directly in bytes
	size_t fileSize = (size_t)file.tellg();

	// spirv expects the buffer to be on uint32, so make sure to reserve a int
	// vector big enough for the entire file
	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

	// put file cursor at beginning
	file.seekg(0);

	// load the entire file into the buffer
	file.read((char*)buffer.data(), fileSize);

	// now that the file is loaded into the buffer, we can close it
	file.close();

	// create a new shader module, using the buffer we loaded
	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;

	// codeSize has to be in bytes, so multply the ints in the buffer by size of
	// int to know the real size of the buffer
	createInfo.codeSize = buffer.size() * sizeof(uint32_t);
	createInfo.pCode = buffer.data();

	// check that the creation goes well.
	VkShaderModule shaderModule;
	if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
		return false;
	}
	*outShaderModule = shaderModule;
	return true;
}

VmaAllocator VulkanUtils::createAllocator(VkPhysicalDevice physicalDevice, VkDevice device, VkInstance instance) {
	VmaAllocatorCreateInfo engineAllocInfo = {
		.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
		.physicalDevice = physicalDevice,
		.device = device,
		.instance = instance
	};
	VmaAllocator allocator;
	VK_CHECK(vmaCreateAllocator(&engineAllocInfo, &allocator));

	return allocator;
}

uint32_t VulkanUtils::GetMaxPushConstantSize(VkPhysicalDevice device) {
	VkPhysicalDeviceProperties props;
	vkGetPhysicalDeviceProperties(device, &props);
	return props.limits.maxPushConstantsSize;
}

PushConstantPool VulkanUtils::CreatePushConstantPool(VkPhysicalDevice device) {
	PushConstantPool pool;
	pool.maxSize = GetMaxPushConstantSize(device);
	pool.usedSize = 0;
	return pool;
}

bool VulkanUtils::AllocatePushConstant(PushConstantPool* pool, uint32_t size, uint32_t* offsetOut) {
	// Align to 4 or 16 bytes
	const uint32_t alignment = 16;
	uint32_t alignedSize = (size + alignment - 1) & ~(alignment - 1);

	if (pool->usedSize + alignedSize > pool->maxSize)
		return false;

	*offsetOut = pool->usedSize;
	pool->usedSize += alignedSize;
	return true;
}