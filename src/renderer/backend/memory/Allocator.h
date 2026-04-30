#pragma once

#include <renderer/backend/memory/VmaForward.h>
#include <renderer/RendererDefinitions.h>
#include <renderer/backend/memory/Staging.h>

namespace RD = RendererDefinitions;

struct AllocatedImage;
struct AllocatedBuffer;
struct DeviceContext;
struct BindlessBDATable;

struct BufferDesc;
struct ImageDesc;

class Allocator final
{
public:
	void Init(const DeviceContext& ctx);
	void Shutdown();

	// ------------------
	// Buffer allocation
	// ------------------

	[[nodiscard]] AllocatedBuffer AllocateBuffer(const BufferDesc& desc);
	void FreeBuffer(AllocatedBuffer buf) const;

	void AllocateGPUBuffer(
		RD::Renderer_Buffer slot,
		BindlessBDATable& addressTable,
		size_t size);

	template<typename T>
	[[nodiscard]] AllocatedBuffer AllocateUniform(const T& data);

	// -----------------
	// Image allocation
	// -----------------
	[[nodiscard]] AllocatedImage AllocateImage(const ImageDesc& desc);
	void FreeImage(AllocatedImage& img);

	const VmaAllocator GetVma() const { return m_vmaAlloc; }

	StagingBuffer FrameStaging; // Around 20mb staging pool

	StagingBuffer GlobalStaging; // Dynamically allocates a size

private:
	VmaAllocator  m_vmaAlloc = VK_NULL_HANDLE;
	DeviceContext m_ctx{};
};

struct BufferDesc
{
	size_t                size          = 0;
	VkBufferUsageFlags    usage         = 0;
	HeapType              heap          = HeapType::GPU_Local;
	bool                  bIsConcurrent = false;
	std::string_view      debugName     = nullptr;
};

struct ImageDesc
{
	VkFormat              format         = VK_FORMAT_UNDEFINED;
	VkExtent3D            extent         = {};
	VkImageUsageFlags     usage          = 0;
	HeapType              heap           = HeapType::GPU_Local;
	VkSampleCountFlagBits samples        = VK_SAMPLE_COUNT_1_BIT;
	uint32_t              mipLevels      = 1;   // 0 = auto-calculate
	uint32_t              arrayLayers    = 1;
	bool                  bIsCubemap     = false;
	bool                  bPerMipStorage = false;
	std::string_view      debugName      = nullptr;
};
