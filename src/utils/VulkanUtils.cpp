#include "pch.h"

#include "VulkanUtils.h"

uint32_t VulkanUtils::FindMemoryType(VkPhysicalDevice pDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(pDevice, &memProperties);

	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
		if ((typeFilter & (1 << i)) &&
			(memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
			return i;
		}
	}

	ASSERT(false && "Failed to find suitable memory type!");
	return 0;
}

QueueFamilyIndices VulkanUtils::FindQueueFamilies(VkPhysicalDevice pDevice, VkSurfaceKHR surface) {
	QueueFamilyIndices indices;

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(pDevice, &queueFamilyCount, nullptr);

	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(pDevice, &queueFamilyCount, queueFamilies.data());

	int i = 0;
	for (const auto& queueFamily : queueFamilies) {
		if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			indices.graphicsFamily = i;
		}

		VkBool32 presentSupport = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(pDevice, i, surface, &presentSupport);
		if (presentSupport) {
			indices.presentFamily = i;
		}

		// Find dedicated transfer queue
		if ((queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) &&
			!(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
			!indices.transferFamily.has_value()) {
			indices.transferFamily = i;
			//fmt::print("Found index {}, dedicated transfer queue !\n", i);
		}

		// Find dedicated compute queue
		if ((queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) &&
			!(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
			!indices.computeFamily.has_value()) {
			indices.computeFamily = i;
			//fmt::print("Found index {}, dedicated compute queue!\n", i);
		}

		if (indices.isComplete()) {
			break;
		}

		i++;
	}

	if (!indices.transferFamily.has_value() && indices.graphicsFamily.has_value()) {
		fmt::print("No dedicated transfer queue, falling back to graphics queue.\n");
		indices.transferFamily = indices.graphicsFamily;
	}

	if (!indices.computeFamily.has_value() && indices.graphicsFamily.has_value()) {
		fmt::print("No dedicated compute queue, falling back to graphics queue.\n");
		indices.computeFamily = indices.graphicsFamily;
	}

	return indices;
}

std::vector<uint32_t> VulkanUtils::findSupportedSampleCounts(VkPhysicalDeviceLimits deviceLimits) {
	std::vector<uint32_t> sampleCounts;

	VkSampleCountFlags counts = deviceLimits.framebufferColorSampleCounts &
		deviceLimits.framebufferDepthSampleCounts;

	if (counts & VK_SAMPLE_COUNT_8_BIT) { sampleCounts.push_back(8); }
	if (counts & VK_SAMPLE_COUNT_4_BIT) { sampleCounts.push_back(4); }
	if (counts & VK_SAMPLE_COUNT_2_BIT) { sampleCounts.push_back(2); }
	sampleCounts.push_back(1); // Always allow no MSAA

	return sampleCounts;
}


VkFormat VulkanUtils::findDepthFormat(VkPhysicalDevice pDevice) {
	return findSupportedFormat(
		pDevice,
		{ VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT },
		VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
	);
}

VkFormat VulkanUtils::findSupportedFormat(VkPhysicalDevice pDevice, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags feature) {
	for (VkFormat format : candidates) {

		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(pDevice, format, &props);

		if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & feature) == feature) {
			return format;
		}
		else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & feature) == feature) {
			return format;
		}
	}

	ASSERT(false && "Failed to find supported format!");
	return VK_FORMAT_UNDEFINED;
}

// TODO: more stencil options
bool VulkanUtils::hasStencilComponent(VkFormat format) {
	return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

bool VulkanUtils::loadShaderModule(const char* filePath, VkDevice device, VkShaderModule* outShaderModule) {
	static std::mutex shaderMutex;
	std::scoped_lock lock(shaderMutex);

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

	// codeSize has to be in bytes, so multiply the ints in the buffer by size of
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

VkDeviceAddress VulkanUtils::getBufferAddress(VkBuffer buffer, VkDevice device) {
	VkBufferDeviceAddressInfo addressInfo{};
	addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	addressInfo.buffer = buffer;

	return vkGetBufferDeviceAddress(device, &addressInfo);
}

VmaAllocator VulkanUtils::createAllocator(VkPhysicalDevice pDevice, VkDevice device, VkInstance instance) {
	static std::mutex allocMutex;
	std::scoped_lock lock(allocMutex);

	VmaAllocatorCreateInfo allocInfo = {
		.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
		.physicalDevice = pDevice,
		.device = device,
		.instance = instance
	};
	VmaAllocator allocator;
	VK_CHECK(vmaCreateAllocator(&allocInfo, &allocator));

	return allocator;
}

void VulkanUtils::defineViewportAndScissor(VkCommandBuffer cmd, VkExtent2D drawExtent) {
	VkViewport viewport = {
		.x = 0.0f,
		.y = 0.0f,
		.width = static_cast<float>(drawExtent.width),
		.height = static_cast<float>(drawExtent.height),
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor = {
		.offset = { 0, 0 },
		.extent = { drawExtent.width, drawExtent.height}
	};
	vkCmdSetScissor(cmd, 0, 1, &scissor);
}