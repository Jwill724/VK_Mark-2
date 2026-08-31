#include "pch.h"

#include "BindlessBDATable.h"
#include "ResourceAllocator.h"

static size_t Index(RD::Renderer_Buffer slot) noexcept
{
	const size_t idx = static_cast<size_t>(slot);
	ASSERT(idx < BindlessBDATable::ADDRESS_TABLE_BUFFER_COUNT);
	return idx;
}

void BindlessBDATable::Init(Allocator& allocator)
{
	ASSERT(allocator.IsInitialized());
	INVARIANT(!m_addressTableBuffer.IsValid()); // must only init once

	BufferDesc addressTableDesc {
		.size = GPU_ADDRESS_TABLE_SIZE_GPU_BYTES,
		.usage = Vulkan_BufferUsage::BDA_POINTER
		//.debugName = "Address_Table"
	};

	m_addressTableBuffer = allocator.AllocateBuffer(addressTableDesc);
}

void BindlessBDATable::Shutdown(Allocator& allocator)
{
	ASSERT(allocator.IsInitialized());
	INVARIANT(m_addressTableBuffer.IsValid());

	for (uint32_t i = 0; i < ADDRESS_TABLE_BUFFER_COUNT; ++i)
	{
		ClearGPUAddressBuffer(static_cast<RD::Renderer_Buffer>(i), allocator);
	}

	allocator.FreeBuffer(m_addressTableBuffer);
}

const AllocatedBuffer& BindlessBDATable::GetGPUBuffer(RD::Renderer_Buffer slot) const
{
	const size_t i = static_cast<size_t>(slot);
	ASSERT(i < ADDRESS_TABLE_BUFFER_COUNT);
	return m_gpuBuffers[i];
}

bool BindlessBDATable::ContainsGPUBuffer(RD::Renderer_Buffer slot) const noexcept
{
	const size_t i = static_cast<size_t>(slot);
	return (i < ADDRESS_TABLE_BUFFER_COUNT) && m_gpuBuffers[i].IsValid() && m_addresses[i] != 0;
}

void BindlessBDATable::AddGPUBufferToAddressTable(RD::Renderer_Buffer slot, size_t size, Allocator& allocator)
{
	ASSERT(allocator.IsInitialized());
	ASSERT(!ContainsGPUBuffer(slot)); // Should never overwrite a valid slot

	auto gpuBuf = allocator.AllocateGPUBuffer(slot, size);
	const uint64_t addr = gpuBuf.m_address;

	SetAddress(slot, addr);
	m_gpuBuffers[Index(slot)] = std::move(gpuBuf);
}

void BindlessBDATable::ClearGPUAddressBuffer(RD::Renderer_Buffer slot, Allocator& allocator)
{
	if (!ContainsGPUBuffer(slot)) return;

	auto& gpuBuf = m_gpuBuffers[Index(slot)];
	allocator.FreeBuffer(gpuBuf);

	gpuBuf.Reset();
	RemoveAddress(slot);
}

void BindlessBDATable::SetGpuVersion(uint32_t version)
{
	ASSERT(m_gpuVersion <= version && "New version invalid.");
	m_gpuVersion = version;
}

void BindlessBDATable::IsVersionMismatched() const { INVARIANT(m_cpuVersion == m_gpuVersion && "GPU is about to use stale address table!"); }


void BindlessBDATable::SetAddress(RD::Renderer_Buffer slot, uint64_t address)
{
	if (m_addresses[Index(slot)] == address && address != 0) return; // Ensure fresh address

	m_addresses[Index(slot)] = address;
	m_bIsTableDirty = true;
}
void BindlessBDATable::RemoveAddress(RD::Renderer_Buffer slot)
{
	if (m_addresses[Index(slot)] == 0) return; // Already removed

	m_addresses[Index(slot)] = 0;
	m_bIsTableDirty = true;
}

void BindlessBDATable::SwapBufferSlots(RD::Renderer_Buffer a, RD::Renderer_Buffer b)
{
	const size_t ia = Index(a);
	const size_t ib = Index(b);

	if (ia == ib) return;
	if (m_addresses[ia] == 0 || m_addresses[ib] == 0) return; // not allocated yet

	std::swap(m_addresses[ia],  m_addresses[ib]);
	std::swap(m_gpuBuffers[ia], m_gpuBuffers[ib]);

	m_bIsTableDirty = true;
}

void BindlessBDATable::ClearAssetBuffers(Allocator& allocator)
{
	ClearGPUAddressBuffer(RD::Renderer_Buffer::Vertex, allocator);
	ClearGPUAddressBuffer(RD::Renderer_Buffer::Index, allocator);
	ClearGPUAddressBuffer(RD::Renderer_Buffer::Mesh, allocator);
	ClearGPUAddressBuffer(RD::Renderer_Buffer::Material, allocator);
	ClearGPUAddressBuffer(RD::Renderer_Buffer::Meshlet, allocator);
	ClearGPUAddressBuffer(RD::Renderer_Buffer::MeshletVertices, allocator);
	ClearGPUAddressBuffer(RD::Renderer_Buffer::MeshletTriangles, allocator);
}

AllocatedBuffer BindlessBDATable::DetachGPUAddressBuffer(RD::Renderer_Buffer slot)
{
	if (!ContainsGPUBuffer(slot)) return {};

	AllocatedBuffer detached = std::move(m_gpuBuffers[Index(slot)]);
	m_gpuBuffers[Index(slot)].Reset();
	RemoveAddress(slot);

	return detached;
}
