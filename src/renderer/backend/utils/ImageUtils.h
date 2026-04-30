#pragma once

#include <renderer/backend/AllocatedImage.h>

namespace ImageUtils
{
	// Within the context of my engine, theres only really types of images, render and textures.
	// Render being something I directly draw and textures going to the gpu, which will need a buffer to do gpu stuff.
	// Texture creation just defers the cmd work and buffer deletion
	// Create texture just holds createrenderimage inside it and skipping the use of a deletion queue is designed only for asset loading
	// since a ModelAsset type should own its resources
	void CreateTexture(
		const VkDevice device,
		VkCommandPool cmdPool,
		const void* data,
		AllocatedImage& renderImage,
		VkImageUsageFlags usage,
		VkSampleCountFlagBits samples,
		DeletionQueue& imageQueue,
		DeletionQueue& bufferQueue,
		const VmaAllocator allocator,
		bool skipQueueUsage = false);
	void CreateRenderTarget(
		const VkDevice device,
		AllocatedImage& renderImage,
		VkImageUsageFlags usage,
		VkSampleCountFlagBits samples,
		DeletionQueue& dq,
		const VmaAllocator alloc,
		bool skipDQ = false);

	void destroyImage(VkDevice device, AllocatedImage& img, const VmaAllocator allocator);

	void transitionImage(
		VkCommandBuffer cmd,
		AllocatedImage& img,
		VkImageLayout newLayout,
		uint32_t baseMip = 0,                                         // Starting mip
		uint32_t mipCount = VK_REMAINING_MIP_LEVELS,                  // How many levels transitioned
		VkImageLayout oldLayoutOverride = VK_IMAGE_LAYOUT_UNDEFINED); // Mips need manual transitions applied

	void imageCopy(
		VkCommandBuffer cmd,
		AllocatedImage& src,
		AllocatedImage& dst,
		VkImageLayout srcFinalLayout,
		VkImageLayout dstFinalLayout,
		bool copyDirect = true); // Keep true if both formats match

	uint32_t calculateMipLevels(AllocatedImage& img, uint32_t maxMipCap = UINT32_MAX);

	size_t getPixelSize(VkFormat format);

	VkImageView CreateImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);

	void generateCubemapMiplevels(VkCommandBuffer cmd, AllocatedImage& image);

	void generateMipmaps(VkCommandBuffer cmd, const AllocatedImage& image);

	VkSampler createSampler(
		const VkDevice device,
		VkFilter filter,
		VkSamplerAddressMode addressMode,
		float maxLod,
		float maxAnisotropy,
		DeletionQueue* dQueue = nullptr,
		VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		bool compareEnabled = false,
		VkBorderColor borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK);
}
