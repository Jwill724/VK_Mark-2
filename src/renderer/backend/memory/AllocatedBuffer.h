#pragma once

#include <vma/vk_mem_alloc.h>
#include <renderer/backend/VulkanTypes.h>

enum class HeapType
{
	GPU_Local,  // VMA_MEMORY_USAGE_GPU_ONLY
	Upload,     // CPU->GPU, persistently mapped
	Readback,   // GPU->CPU
	Staging,    // Transient upload, pooled internally
	Count
};

struct AllocatedBuffer
{
	VkBuffer              m_buffer        = VK_NULL_HANDLE;
	VkDeviceAddress       m_address       = UINT64_MAX;
	VmaAllocation         m_allocation    = VK_NULL_HANDLE;
	VmaAllocationInfo     m_allocInfo{};
	void*                 m_mappedPtr     = nullptr;
	size_t                m_bytesSize     = 0;

	VkBufferUsageFlags    m_usage         = 0;
	HeapType              m_heap          = HeapType::GPU_Local;

	Vulkan_DescriptorType m_type          = Vulkan_DescriptorType::SSBO;
	bool                  m_bIsConcurrent = false;
	uint8_t               m_qmask         = 0;

	bool IsValid()  const noexcept { return m_buffer != VK_NULL_HANDLE; }
	void Reset() { *this = AllocatedBuffer{}; }

	static size_t AlignUp(size_t value, size_t alignment) noexcept
	{
		return (value + alignment - 1) & ~(alignment - 1);
	}
};

// Bindless indirect table, stores ssbo bda pointers.
// Upload address table buffer after new addresses are attached or removed to the table.
// Buffer cleanup needs to be done by the Allocator class, where AllocatedBuffer types are created
class BindlessBDATable
{
public:
	static constexpr size_t GPU_ADDRESS_TABLE_SIZE_GPU_BYTES = static_cast<size_t>(RD::Renderer_Buffer::Count) * sizeof(uint64_t);

	// Set this one time at start, lasts for duration of renderer lifetime.
	void Init(AllocatedBuffer addressTableBuffer)
	{
		INVARIANT(addressTableBuffer.IsValid());
		INVARIANT(!m_addressTableBuffer.IsValid()); // must only init once

		if (!m_addressTableBuffer.IsValid())
			m_addressTableBuffer = addressTableBuffer;

		INVARIANT(m_addressTableBuffer.IsValid());
	}

	const std::array<uint64_t, static_cast<size_t>(RendererDefinitions::Renderer_Buffer::Count)>& GetTable() const { return m_addrs; }

	AllocatedBuffer& GetGPUAddressBuffer(RD::Renderer_Buffer slot) { return m_gpuBuffers[static_cast<size_t>(slot)]; }
	bool ContainsGPUBuffer(RD::Renderer_Buffer slot) const
	{
		return m_gpuBuffers[static_cast<size_t>(slot)].IsValid();
	}
	void AddGPUBufferToAddressTable(RD::Renderer_Buffer slot, AllocatedBuffer gpuBuffer)
	{
		m_gpuBuffers[static_cast<size_t>(slot)] = gpuBuffer;
		SetAddress(slot, gpuBuffer.m_address);
	}

	void ResetGPUAddressBuffer(RD::Renderer_Buffer slot)
	{
		m_gpuBuffers[static_cast<size_t>(slot)].Reset();
		RemoveAddress(slot);
	}

	bool IsTableDirty() const { return m_bIsTableDirty; }

	// Called after descriptor update
	void ClearTableDirty() { m_bIsTableDirty = false; }

	uint32_t GetCpuVersion() const { return m_cpuVersion; }
	void UpdateCpuVersion() { m_cpuVersion++; }

	uint32_t GetGpuVersion() const { return m_gpuVersion; }

	void SetGpuVersion(uint32_t version)
	{
		// Shouldn't ever happen
		if (m_gpuVersion > version) return;

		m_gpuVersion = version;
	}

	bool IsVersionMismatched() const noexcept { return m_cpuVersion != m_gpuVersion; }

private:
	void SetAddress(RendererDefinitions::Renderer_Buffer type, uint64_t address)
	{
		size_t index = static_cast<size_t>(type);

		if (m_addrs[index] == address) return;

		m_addrs[index]  = address;
		m_bIsTableDirty = true;
	}
	void RemoveAddress(RendererDefinitions::Renderer_Buffer type)
	{
		size_t index = static_cast<size_t>(type);

		if (m_addrs[index] == 0) return;

		m_addrs[index]  = 0;
		m_bIsTableDirty = true;
	}

	std::array<uint64_t, static_cast<size_t>(RendererDefinitions::Renderer_Buffer::Count)> m_addrs; // Primary array that stores buffer pointers
	std::array<AllocatedBuffer, static_cast<size_t>(RD::Renderer_Buffer::Count)> m_gpuBuffers;

	uint32_t m_cpuVersion    = 1u; // increment when modified
	uint32_t m_gpuVersion    = 0u; // last uploaded version
	bool     m_bIsTableDirty = false;

	AllocatedBuffer m_addressTableBuffer;
};
