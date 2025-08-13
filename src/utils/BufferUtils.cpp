#include "pch.h"

#include "utils/BufferUtils.h"
#include "vulkan/Backend.h"

AllocatedBuffer BufferUtils::createBuffer(
	size_t allocSize,
	VkBufferUsageFlags usage,
	VmaMemoryUsage memoryUsage,
	const VmaAllocator allocator,
	bool concurrentSharingOn)
{
	static std::mutex mutex;
	std::scoped_lock lock(mutex);

	AllocatedBuffer newBuffer;

	if (allocSize == 0) {
		fmt::print("[BufferUtils] Warning: Attempting to create 0-byte buffer.\n");
		allocSize = 4;
	}

	VkBufferCreateInfo bufferInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.pNext = nullptr;
	bufferInfo.size = allocSize;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	// buffer to be shared across different queues
	std::array<uint32_t, 3> qFamilies{};
	uint32_t qFamCount = 0;
	uint8_t mask = 0;

	if (concurrentSharingOn) {
		const uint32_t g = Backend::getGraphicsQueue().familyIndex;
		const uint32_t t = Backend::getTransferQueue().familyIndex;
		const uint32_t c = Backend::getComputeQueue().familyIndex;

		auto pushUnique = [&](uint32_t fam, uint8_t bit) {
			for (uint32_t i = 0; i < qFamCount; ++i) {
				if (qFamilies[i] == fam) return; // already present
			}
			qFamilies[qFamCount++] = fam;
			mask |= bit;
		};

		pushUnique(g, 0x1);
		pushUnique(t, 0x2);
		pushUnique(c, 0x4);

		if (qFamCount > 1) {
			bufferInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
			bufferInfo.queueFamilyIndexCount = qFamCount;
			bufferInfo.pQueueFamilyIndices = qFamilies.data();
		}
	}

	newBuffer.isConcurrent = (qFamCount > 1);
	newBuffer.qmask = mask;

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

	if (vmaallocInfo.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT) {
		newBuffer.mapped = newBuffer.info.pMappedData;
		ASSERT(newBuffer.mapped != nullptr);
	}

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

	if (AddressBufferType::OpaqueIndirectDraws == addressBufferType ||
		AddressBufferType::TransparentIndirectDraws == addressBufferType) {
		usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
	}

	if (AddressBufferType::Vertex == addressBufferType) {
		usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	}

	if (AddressBufferType::Index == addressBufferType) {
		usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	}

	if (AddressBufferType::VisibleCount == addressBufferType ||
		AddressBufferType::VisibleMeshIDs == addressBufferType)
	{
		usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	}

	AllocatedBuffer buffer = createBuffer(
		size,
		usage,
		VMA_MEMORY_USAGE_GPU_ONLY,
		allocator,
		true
	);

	addressTable.setAddress(addressBufferType, buffer.address);

	return buffer;
}

void BufferUtils::destroyBuffer(VkBuffer buffer, VmaAllocation allocation, const VmaAllocator allocator) {
	static std::mutex mutex;
	std::scoped_lock lock(mutex);
	vmaDestroyBuffer(allocator, buffer, allocation);
	//fmt::print("[DestroyBuffer] Buffer = {}, Memory = {}\n", (void*)buffer, (void*)allocation);
}

void BufferUtils::destroyAllocatedBuffer(AllocatedBuffer& buffer, const VmaAllocator allocator) {
	static std::mutex mutex;
	std::scoped_lock lock(mutex);
	vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
	//fmt::print("[DestroyAllocatedBuffer] Buffer = {}, Memory = {}\n", (void*)buffer.buffer, (void*)buffer.allocation);

	buffer.buffer = VK_NULL_HANDLE;
	buffer.allocation = nullptr;
	buffer.mapped = nullptr;
}