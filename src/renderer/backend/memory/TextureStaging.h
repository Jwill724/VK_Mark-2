#pragma once

#include "AllocatedImage.h"
#include "../VulkanTypes.h"

enum class MipStrategy : uint8_t
{
	SingleLevel,      // Upload mip 0 only, transition to ShaderRead
	GenerateOnGPU,    // Upload mip 0, blit-generate remaining mips
	//	PrebuiltMips    // upload all mips from CPU data (KTX/Basis path)
};
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
	AllocatedImage*                 image    = nullptr;
	std::vector<StagedTextureWrite> writes   = {};
	MipStrategy                     strategy = MipStrategy::GenerateOnGPU;
};

struct TextureUploadDesc
{
	AllocatedImage* image      = nullptr;
	const void*     pixelData  = nullptr;  // mip 0 always required
	size_t          pixelBytes = 0;        // bytes per pixel
	MipStrategy     strategy   = MipStrategy::GenerateOnGPU;

	bool IsValid() const noexcept
	{
		return image && image->IsValid() && pixelData && pixelBytes > 0;
	}
};
