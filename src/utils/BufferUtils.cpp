#include "pch.h"

#include "utils/BufferUtils.h"
#include "vulkan/Backend.h"

AllocatedBuffer BufferUtils::createBuffer(size_t allocSize, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage, const VmaAllocator allocator) {
	static std::mutex mutex;
	std::scoped_lock lock(mutex);

	AllocatedBuffer newBuffer;

	if (allocSize == 0) {
		fmt::print("[BufferUtils] Warning: Attempting to create 0-byte buffer.\n");
		allocSize = 4; // Vulkan requires non-zero buffer size
	}

	VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.pNext = nullptr;
	bufferInfo.size = allocSize;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo vmaallocInfo{};
	vmaallocInfo.usage = memoryUsage;
	vmaallocInfo.flags = 0;

	if (memoryUsage == VMA_MEMORY_USAGE_CPU_ONLY ||
		memoryUsage == VMA_MEMORY_USAGE_CPU_TO_GPU ||
		memoryUsage == VMA_MEMORY_USAGE_GPU_TO_CPU ||
		memoryUsage == VMA_MEMORY_USAGE_AUTO_PREFER_HOST)
	{
		vmaallocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT |
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
	}

	if (memoryUsage == VMA_MEMORY_USAGE_GPU_ONLY ||
		memoryUsage == VMA_MEMORY_USAGE_AUTO ||
		memoryUsage == VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE)
	{
		vmaallocInfo.flags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;
	}

	if (allocSize >= (static_cast<size_t>(512 * 1024))) {
		vmaallocInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
	}

	VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &vmaallocInfo, &newBuffer.buffer, &newBuffer.allocation, &newBuffer.info));

	if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
		VkBufferDeviceAddressInfo addressInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		addressInfo.buffer = newBuffer.buffer;
		newBuffer.address = vkGetBufferDeviceAddress(Backend::getDevice(), &addressInfo);
	}
	else {
		newBuffer.address = 0;
	}

	if ((vmaallocInfo.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT) && newBuffer.info.pMappedData == nullptr) {
		fmt::print("[BufferUtils] Warning: Mapped flag set but pMappedData is nullptr\n");
	}

	newBuffer.mapped = newBuffer.info.pMappedData;
	return newBuffer;
}

AllocatedBuffer BufferUtils::createGPUAddressBuffer(AddressBufferType addressBufferType,
	GPUAddressTable& addressTable, size_t size, const VmaAllocator allocator) {
	static std::mutex mutex;
	std::scoped_lock lock(mutex);

	VkBufferUsageFlags usage =
		VK_BUFFER_USAGE_TRANSFER_DST_BIT |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

	if (AddressBufferType::IndirectCmd == addressBufferType) {
		usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
	}

	AllocatedBuffer buffer = createBuffer(
		size,
		usage,
		VMA_MEMORY_USAGE_GPU_ONLY,
		allocator
	);

	switch (addressBufferType) {
	case AddressBufferType::Instance:
		addressTable.instanceBuffer = buffer.address;
		break;
	case AddressBufferType::IndirectCmd:
		addressTable.indirectCmdBuffer = buffer.address;
		break;
	case AddressBufferType::DrawRange:
		addressTable.drawRangeBuffer = buffer.address;
		break;
	case AddressBufferType::Material:
		addressTable.materialBuffer = buffer.address;
		break;
	}

	return buffer;
}


void BufferUtils::destroyBuffer(AllocatedBuffer buffer, const VmaAllocator allocator) {
	static std::mutex mutex;
	std::scoped_lock lock(mutex);
	vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
	fmt::print("[Destroy] Buffer = {}, Memory = {}\n", (void*)buffer.buffer, (void*)buffer.allocation);

	buffer.buffer = VK_NULL_HANDLE;
	buffer.allocation = nullptr;
	buffer.mapped = nullptr;
}