#pragma once

#include "common/ResourceTypes.h"

namespace BufferUtils {
	// Designed for storage buffer address creation
	AllocatedBuffer createGPUAddressBuffer(AddressBufferType addressBufferType,
		GPUAddressTable& addressTable, size_t size, const VmaAllocator allocator);
	AllocatedBuffer createBuffer(
		size_t allocSize,
		VkBufferUsageFlags usage,
		VmaMemoryUsage memoryUsage,
		const VmaAllocator allocator,
		bool concurrentSharingOn = false);

	// For more discrete types where data reset occurs
	void destroyAllocatedBuffer(AllocatedBuffer& buffer, const VmaAllocator allocator);

	// Temporary by value destruction when out of scope
	void destroyBuffer(VkBuffer buffer, VmaAllocation allocation, const VmaAllocator allocator);
}