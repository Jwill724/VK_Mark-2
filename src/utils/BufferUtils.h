#pragma once

#include "Engine.h"

namespace BufferUtils {
	AllocatedBuffer createGPUAddressBuffer(AddressBufferType addressBufferType,
		GPUAddressTable& addressTable, size_t size, const VmaAllocator allocator);
	AllocatedBuffer createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, const VmaAllocator allocator);
	void destroyBuffer(AllocatedBuffer buffer, const VmaAllocator allocator);
}