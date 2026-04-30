#include "pch.h"

#include "BufferUtils.h"
#include "renderer/backend/resources/AllocatedBuffer.h"
#include "Core.h"

AllocatedBuffer BufferUtils::CreateBuffer(
	size_t allocSize,
	VkFlags usage,
	VmaMemoryUsage memoryUsage,
	const VmaAllocator allocator,
	bool concurrentSharingOn)
{
	AllocatedBuffer newBuffer;

	if (allocSize == 0)
	{
		fmt::print("[BufferUtils] Warning: Attempting to create 0-byte buffer.\n");
		allocSize = 4;
	}

	VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.pNext = nullptr;
	bufferInfo.size = allocSize;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	// buffer to be shared across different queues
	std::array<uint32_t, 3> qFamilies{};
	uint32_t qFamCount = 0;
	uint8_t mask = 0;

	if (concurrentSharingOn)
	{
		const uint32_t g = Backend::GetGraphicsQueue().GetFamilyIndex();
		const uint32_t t = Backend::GetTransferQueue().GetFamilyIndex();
		const uint32_t c = Backend::GetComputeQueue().GetFamilyIndex();

		auto PushUnique = [&](uint32_t fam, uint8_t bit) {
			for (uint32_t i = 0; i < qFamCount; ++i)
			{
				if (qFamilies[i] == fam) return; // already present
			}
			qFamilies[qFamCount++] = fam;
			mask |= bit;
		};

		PushUnique(g, 0x1);
		PushUnique(t, 0x2);
		PushUnique(c, 0x4);

		if (qFamCount > 1)
		{
			bufferInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
			bufferInfo.queueFamilyIndexCount = qFamCount;
			bufferInfo.pQueueFamilyIndices = qFamilies.data();
		}
	}

	newBuffer.m_bIsConcurrent = (qFamCount > 1);
	newBuffer.m_qmask = mask;

	VmaAllocationCreateInfo vmaallocInfo{};
	vmaallocInfo.usage = memoryUsage;
	vmaallocInfo.flags = 0;

	switch(memoryUsage)
	{
	case VMA_MEMORY_USAGE_CPU_ONLY:
	case VMA_MEMORY_USAGE_CPU_TO_GPU:
	case VMA_MEMORY_USAGE_AUTO_PREFER_HOST:
		vmaallocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT |
			VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
		break;

	case VMA_MEMORY_USAGE_GPU_TO_CPU:
		vmaallocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT |
			VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
		break;

	case VMA_MEMORY_USAGE_GPU_ONLY:
	case VMA_MEMORY_USAGE_AUTO:
	case VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE:
		vmaallocInfo.flags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;
		break;
	}

	if (allocSize >= (static_cast<size_t>(512 * 1024))) {
		vmaallocInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
	}

	VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &vmaallocInfo, &newBuffer.m_buffer, &newBuffer.m_allocation, &newBuffer.m_allocInfo));

	if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
	{
		VkBufferDeviceAddressInfo addressInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		addressInfo.buffer = newBuffer.m_buffer;
		newBuffer.m_address = vkGetBufferDeviceAddress(Backend::GetDevice(), &addressInfo);
	}
	else
	{
		newBuffer.m_address = 0;
	}

	if (vmaallocInfo.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT)
	{
		newBuffer.m_mappedPtr = newBuffer.m_allocInfo.pMappedData;
		ASSERT(newBuffer.m_mappedPtr != nullptr);
	}

	return newBuffer;
}

AllocatedBuffer BufferUtils::CreateGPUAddressBuffer(
	Renderer_Buffer bufferSlot,
	BindlessBufferTable& bufferTable,
	size_t size,
	const VmaAllocator allocator)
{
	// Baseline usages
	VkBufferUsageFlags usage = static_cast<VkBufferUsageFlags>(BufferUsage::ADDRESS_TABLE);

	// Special usages
	switch(bufferSlot)
	{
	case Renderer_Buffer::IndirectDraws:
	case Renderer_Buffer::DispatchIndirectArgs:
		usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
		break;
	case Renderer_Buffer::Vertex:
		usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		break;
	case Renderer_Buffer::Index:
		usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		break;
	case Renderer_Buffer::Transforms: // Transforms copies into previous transforms
		usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		break;
	}

	AllocatedBuffer buffer = CreateBuffer(
		size,
		usage,
		VMA_MEMORY_USAGE_GPU_ONLY,
		allocator,
		true // gpu buffers will be shared among queues
	);

	// Marks the table dirty with each addition
	bufferTable.SetAddress(bufferSlot, buffer.m_address);

	return buffer;
}

template<typename UniformType>
AllocatedBuffer BufferUtils::CreateUniformBuffer(
	const UniformType& type,
	const VmaAllocator allocator)
{
	const size_t bufferBytes = sizeof(UniformType);

	AllocatedBuffer uniformBuffer = CreateBuffer(
		bufferBytes,
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VMA_MEMORY_USAGE_CPU_TO_GPU,
		allocator);
	uniformBuffer.m_type = Vulkan_DescriptorType::UNIFORM;

	UniformType* typePtr = reinterpret_cast<UniformType*>(uniformBuffer.m_mappedPtr);
	memcpy(typePtr, &type, bufferBytes);

	vmaFlushAllocation(allocator, uniformBuffer.m_allocation, 0, bufferBytes);

	return uniformBuffer;
}

AllocatedBuffer BufferUtils::CreateGPUStagingBuffer(
	size_t size,
	const VmaAllocator allocator)
{
	AllocatedBuffer stagingBuffer = CreateBuffer(
		size,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
		allocator
	);
	stagingBuffer.m_type = Vulkan_DescriptorType::COPY_STAGING;

	return stagingBuffer;
}

void BufferUtils::DestroyBuffer(
	VkBuffer buffer,
	VmaAllocation allocation,
	const VmaAllocator allocator)
{
	vmaDestroyBuffer(allocator, buffer, allocation);
}

void BufferUtils::DestroyAllocatedBuffer(
	AllocatedBuffer& buffer,
	const VmaAllocator allocator)
{
	if (buffer.IsValid())
	{
		vmaDestroyBuffer(allocator, buffer.m_buffer, buffer.m_allocation);
		buffer.Reset();
	}
}

size_t BufferUtils::AlignUp(size_t x, size_t a) { return (x + (a - 1)) & ~(a - 1); }

size_t BufferUtils::ReserveStaging(
	size_t& stagingHead,
	size_t totalStagingSize,
	size_t stageBytes)
{
	const size_t atom = Backend::GetNonCoherentAtomSize();
	const size_t alignment = (atom > 16) ? atom : 16; // 16-byte min alignment
	ASSERT((alignment & (alignment - 1)) == 0 && "[staging] alignment must be pow2");
	ASSERT((stageBytes % 4) == 0 && "[staging] require 4-byte size");

	const size_t offset = AlignUp(stagingHead, alignment);
	ASSERT(offset + stageBytes <= totalStagingSize && "[staging] overflow");
	stagingHead = offset + stageBytes;
	return offset;
}

void BufferUtils::FlushStagingRange(
	const VmaAllocation bufAllocation,
	size_t offset,
	size_t bytes,
	const VmaAllocator allocator)
{
	const size_t atom = Backend::GetNonCoherentAtomSize();
	const size_t begin = offset & ~(atom - 1);
	const size_t end = AlignUp(offset + bytes, atom);
	vmaFlushAllocation(allocator, bufAllocation, begin, end - begin);
}
