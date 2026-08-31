#pragma once

#include "AllocatedImage.h"
#include "../VulkanTypes.h"

enum class MipStrategy : uint8_t
{
	SingleLevel,    // Upload mip 0 only, transition to ShaderRead
	GenerateOnGPU,  // Upload mip 0, blit-generate remaining mips
	Precomputed,    // upload all mips from CPU data
};

enum class TextureFormat : uint32_t
{
	RGBA8 = 0,
	BC7,
	BC5,
};

struct TextureMipDesc
{
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t offset = 0;
	uint32_t bytes = 0;
};

inline size_t MipByteSize(TextureFormat f, uint32_t w, uint32_t h)
{
	if (f == TextureFormat::RGBA8) return static_cast<size_t>(w) * h * 4u;
	return static_cast<size_t>((w + 3u) / 4u) * ((h + 3u) / 4u) * 16u;
}

struct StagedTextureWrite
{
	VkBuffer     srcBuffer  = VK_NULL_HANDLE;
	VkImage      dstImage   = VK_NULL_HANDLE;
	VkDeviceSize srcOffset  = 0;
	VkExtent3D   extent     = {};
	uint32_t     mipLevel   = 0;
	uint32_t     baseLayer  = 0;
	uint32_t     layerCount = 1;

	VkBufferImageCopy ToBufferImageCopy() const noexcept
	{
		VkBufferImageCopy copy{};
		copy.bufferOffset                    = srcOffset;
		copy.bufferRowLength                 = 0;
		copy.bufferImageHeight               = 0;
		copy.imageSubresource.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
		copy.imageSubresource.mipLevel       = mipLevel;
		copy.imageSubresource.baseArrayLayer = baseLayer;
		copy.imageSubresource.layerCount     = layerCount;
		copy.imageOffset                     = { 0, 0, 0 };
		copy.imageExtent                     = extent;
		return copy;
	}
};

struct PendingTextureUpload
{
	AllocatedImage* image = nullptr;
	std::vector<StagedTextureWrite> writes;
	MipStrategy                     strategy = MipStrategy::SingleLevel;
};

struct TextureUploadDesc
{
	AllocatedImage* image = nullptr;
	const void* pixelData = nullptr;
	size_t                          pixelBytes = 0;   // legacy: bytes per texel
	size_t                          byteSize = 0;   // explicit total; 0 = derive
	std::span<const TextureMipDesc> mips;             // empty = single level 0
	MipStrategy                     strategy = MipStrategy::SingleLevel;

	bool IsValid() const noexcept
	{
		return image != nullptr
			&& image->IsValid()
			&& pixelData != nullptr
			&& (byteSize > 0 || pixelBytes > 0);
	}
};
