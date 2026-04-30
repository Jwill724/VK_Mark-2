#include "pch.h"

#include "Allocator.h"
#include "Core.h"
#include <renderer/backend/memory/AllocatedImage.h>
#include <renderer/backend/memory/AllocatedBuffer.h>

using RB = FIX8::conjure_enum<RD::Renderer_Buffer>;

void Allocator::Init(const DeviceContext& ctx)
{
	VmaAllocatorCreateInfo allocInfo {
		.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
		.physicalDevice = ctx.physicalDevice,
		.device = ctx.device,
		.instance = ctx.instance
	};
	VK_CHECK(vmaCreateAllocator(&allocInfo, &m_vmaAlloc));
}

void Allocator::Shutdown()
{
	FrameStaging.Shutdown();
	GlobalStaging.Shutdown();
	if (m_vmaAlloc != nullptr)
		vmaDestroyAllocator(m_vmaAlloc);
}

void Allocator::AllocateGPUBuffer(
	RD::Renderer_Buffer slot,
	BindlessBufferTable& addressTable,
	size_t size)
{
	BufferDesc desc{};
	desc.size          = size;
	desc.usage         = static_cast<VkBufferUsageFlags>(BufferUsage::ADDRESS_TABLE);
	desc.heap          = HeapType::GPU_Local;
	desc.bIsConcurrent = true;
	desc.debugName     = RB::enum_to_string(slot, true);

	switch (slot)
	{
		case RD::Renderer_Buffer::IndirectDraws:
		case RD::Renderer_Buffer::DispatchIndirectArgs:
			desc.usage |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
			break;
		case RD::Renderer_Buffer::Vertex:
			desc.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
			break;
		case RD::Renderer_Buffer::Index:
			desc.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
			break;
		case RD::Renderer_Buffer::Transforms:
			desc.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			break;
		default: break;
	}

	addressTable.AddGPUBufferToAddressTable(slot, AllocateBuffer(desc));
}

template<typename T>
AllocatedBuffer Allocator::AllocateUniform(const T& data)
{
	BufferDesc desc{};
	desc.size  = sizeof(T);
	desc.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	desc.heap  = HeapType::Upload;

	AllocatedBuffer buf = AllocateBuffer(desc);
	buf.m_type = Vulkan_DescriptorType::UNIFORM;

	ASSERT(buf.m_mappedPtr != nullptr);
	memcpy(buf.m_mappedPtr, &data, sizeof(T));
	vmaFlushAllocation(m_vmaAlloc, buf.m_allocation, 0, sizeof(T));
	return buf;
}

AllocatedBuffer Allocator::AllocateBuffer(const BufferDesc& desc)
{
	AllocatedBuffer newBuffer;

	auto allocSize = desc.size;

	if (desc.size == 0)
	{
		fmt::print("[Allocator::AllocateBuffer] Warning: Attempting to create 0-byte buffer.\n");
		allocSize = 4;
	}

	VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferInfo.pNext = nullptr;
	bufferInfo.size = allocSize;
	bufferInfo.usage = desc.usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	// buffer to be shared across different queues
	std::array<uint32_t, 3> qFamilies{};
	uint32_t qFamCount = 0;
	uint8_t mask = 0;

	if (desc.bIsConcurrent)
	{
		const uint32_t g = m_ctx.queueIndices.graphicsFamily.value();
		const uint32_t t =  m_ctx.queueIndices.transferFamily.value();
		const uint32_t c=  m_ctx.queueIndices.computeFamily.value();
 
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

	auto vmaUsage = HeapTypeToVma(desc.heap);

	VmaAllocationCreateInfo vmaallocInfo{};
	vmaallocInfo.usage = vmaUsage;
	vmaallocInfo.flags = HeapTypeToVmaFlags(desc.heap, desc.size);

	VK_CHECK(vmaCreateBuffer(m_vmaAlloc, &bufferInfo, &vmaallocInfo, &newBuffer.m_buffer, &newBuffer.m_allocation, &newBuffer.m_allocInfo));

	newBuffer.m_address = 0;
	if (desc.usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
	{
		VkBufferDeviceAddressInfo addressInfo{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
		addressInfo.buffer = newBuffer.m_buffer;
		newBuffer.m_address = vkGetBufferDeviceAddress(m_ctx.device, &addressInfo);
	}

	if (vmaallocInfo.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT)
	{
		newBuffer.m_mappedPtr = newBuffer.m_allocInfo.pMappedData;
		ASSERT(newBuffer.m_mappedPtr != nullptr);
	}

	return newBuffer;
}


VmaMemoryUsage Allocator::HeapTypeToVma(HeapType heap) const noexcept
{
	switch (heap)
	{
		case HeapType::GPU_Local:
			return VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
		case HeapType::Upload:
		case HeapType::Readback:
		case HeapType::Staging:
			 return VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
		default:
			return VMA_MEMORY_USAGE_AUTO;
	}
}

VmaAllocationCreateFlags Allocator::HeapTypeToVmaFlags(HeapType heap, size_t size) const noexcept
{
	VmaAllocationCreateFlags flags = 0;

	switch (heap)
	{
		case HeapType::Upload:
		case HeapType::Staging:
			flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT |
					 VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
			break;
		case HeapType::Readback:
			flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT |
					 VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
			break;
		case HeapType::GPU_Local:
			flags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;
			break;
	}

	if (size >= (512 * 1024))
		flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

	return flags;
}
