#include "pch.h"

#include "utils/BufferUtils.h"
#include "vulkan/Backend.h"

AllocatedBuffer BufferUtils::createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VmaAllocator& allocator) {
	AllocatedBuffer newBuffer;

	VkBufferCreateInfo bufferInfo = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.pNext = nullptr;
	bufferInfo.size = allocSize;

	bufferInfo.usage = usage;

	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = memoryUsage;
	vmaallocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &vmaallocInfo, &newBuffer.buffer, &newBuffer.allocation, &newBuffer.info));

	// always mapping it
	newBuffer.mapped = newBuffer.info.pMappedData;

	// If the memory type should be mapped, ensure it actually mapped
	if (memoryUsage == VMA_MEMORY_USAGE_CPU_ONLY ||
		memoryUsage == VMA_MEMORY_USAGE_CPU_TO_GPU ||
		memoryUsage == VMA_MEMORY_USAGE_GPU_TO_CPU)
	{
		assert(newBuffer.mapped != nullptr && "Expected mapped memory, but got nullptr!");
	}

	return newBuffer;
}

// Idk why I have this in here but fuck it
VkImageView BufferUtils::createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels) {
	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = aspectFlags;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = mipLevels;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	VkImageView imageView;
	VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &imageView));

	return imageView;
}

void BufferUtils::destroyBuffer(const AllocatedBuffer& buffer, VmaAllocator& allocator) {
	vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
}