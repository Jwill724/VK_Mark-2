#pragma once

#include "Engine.h"

namespace BufferUtils {
	VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);
	AllocatedBuffer createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, VmaAllocator& allocator);
	void destroyBuffer(const AllocatedBuffer& buffer, VmaAllocator& allocator);
}