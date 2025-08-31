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

	template<typename UniformType>
	inline AllocatedBuffer createUniformBuffer(const UniformType& type, const VmaAllocator allocator) {
		const size_t bufferBytes = sizeof(UniformType);

		AllocatedBuffer uniformBuffer = createBuffer(
			bufferBytes,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VMA_MEMORY_USAGE_CPU_TO_GPU,
			allocator);

		ASSERT(uniformBuffer.buffer != VK_NULL_HANDLE);
		ASSERT(uniformBuffer.mapped != nullptr);

		UniformType* typePtr = reinterpret_cast<UniformType*>(uniformBuffer.mapped);
		memcpy(typePtr, &type, bufferBytes);

		vmaFlushAllocation(allocator, uniformBuffer.allocation, 0, bufferBytes);

		return uniformBuffer;
	}

	// For more discrete types where data reset occurs
	void destroyAllocatedBuffer(AllocatedBuffer& buffer, const VmaAllocator allocator);

	// Temporary by value destruction when out of scope
	void destroyBuffer(VkBuffer buffer, VmaAllocation allocation, const VmaAllocator allocator);


	// Staging buffer helpers
	size_t reserveStaging(size_t& stagingHead, size_t totalStagingSize, size_t stageBytes);
	size_t alignUp(size_t x, size_t a);

	// Flush a written host range
	void flushStagingRange(const VmaAllocation bufAllocation, size_t offset, size_t bytes, const VmaAllocator allocator);
}