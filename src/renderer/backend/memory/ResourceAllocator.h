#pragma once

#include "VmaForward.h"
#include "../../RendererDefinitions.h"
#include "Staging.h"
#include "../VulkanTypes.h"

namespace RD = RendererDefinitions;

struct AllocatedImage;
struct AllocatedBuffer;
struct VRAMStats;
class BindlessImageTable;

class Allocator final
{
public:
	void Init(const DeviceContext& ctx);
	void Shutdown();

	VRAMStats GetTotalVRAMUsage() const;

	// ------------------
	// Buffer allocation
	// ------------------

	AllocatedBuffer AllocateGPUBuffer(
		RD::Renderer_Buffer slot,
		size_t size);

	template<typename T>
	[[nodiscard]] AllocatedBuffer AllocateUniform(const T& data)
	{
		static_assert(std::is_trivially_copyable_v<T>);
		return AllocateUniformRaw(&data, sizeof(T));
	}

	// Core buffer life cycle
	[[nodiscard]] AllocatedBuffer AllocateBuffer(const BufferDesc& desc);
	void FreeBuffer(AllocatedBuffer buf) const;

	// -----------------
	// Image allocation
	// -----------------

	// Core image life cycle
	[[nodiscard]] AllocatedImage AllocateImage(const ImageDesc& desc) const;
	void FreeImage(AllocatedImage& img) const;

	const VmaAllocator GetVma() { return m_vmaAlloc; }

	StagingBuffer FrameStaging; // Around 20mb staging pool
	void InitFrameStaging(size_t size, size_t atomicSize)
	{
		FrameStaging.Init(size, m_vmaAlloc, m_deviceCtx.device, atomicSize);
	}

	StagingBuffer GlobalStaging; // Dynamically allocates a size
	void InitGlobalStaging(size_t size, size_t atomicSize)
	{
		GlobalStaging.Init(size, m_vmaAlloc, m_deviceCtx.device, atomicSize);
	}

	void ResetGlobalStaging(size_t size, size_t atomicSize);

	size_t CalcBaseGlobalStagingSize(const BindlessImageTable& imageTable) const;

	bool IsInitialized() const noexcept { return m_vmaAlloc != VK_NULL_HANDLE; }

private:
	[[nodiscard]] AllocatedBuffer AllocateUniformRaw(const void* data, size_t size);
	VmaAllocator  m_vmaAlloc = VK_NULL_HANDLE;
	DeviceContext m_deviceCtx{};
};
