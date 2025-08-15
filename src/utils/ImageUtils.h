#pragma once

#include "common/ResourceTypes.h"
#include "common/EngineTypes.h"

namespace ImageUtils {
	// Within the context of my engine, theres only really types of images, render and textures.
	// Render being something I directly draw and textures going to the gpu, which will need a buffer to do gpu stuff.
	// Texture creation just defers the cmd work and buffer deletion
	// Create texture just holds createrenderimage inside it and skipping the use of a deletion queue is designed only for asset loading
	// since a ModelAsset type should own its resources
	void createTextureImage(
		const VkDevice device,
		VkCommandPool cmdPool,
		void* data,
		AllocatedImage& renderImage,
		VkImageUsageFlags usage,
		VkSampleCountFlagBits samples,
		DeletionQueue& imageQueue,
		DeletionQueue& bufferQueue,
		const VmaAllocator allocator,
		bool skipQueueUsage = false);
	void createRenderImage(
		const VkDevice device,
		AllocatedImage& renderImage,
		VkImageUsageFlags usage,
		VkSampleCountFlagBits samples,
		DeletionQueue& dq,
		const VmaAllocator alloc,
		bool skipDQ = false);

	void destroyImage(VkDevice device, AllocatedImage& img, const VmaAllocator allocator);

	void transitionImage(
		VkCommandBuffer cmd, VkImage image, VkFormat format,
		VkImageLayout currentLayout, VkImageLayout newLayout,
		VkPipelineStageFlags2 dstStageOverride = 0,
		VkAccessFlags2        dstAccessOverride = 0);
	void copyImageToImage(VkCommandBuffer cmd, VkImage source, VkImage destination, VkExtent2D srcSize, VkExtent2D dstSize);

	uint32_t calculateMipLevels(AllocatedImage& img, uint32_t maxMipCap = UINT32_MAX);

	size_t getPixelSize(VkFormat format);

	VkImageView createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);

	void generateCubemapMiplevels(VkCommandBuffer cmd, AllocatedImage& image);

	void generateMipmaps(VkCommandBuffer cmd, const AllocatedImage& image);

	VkSampler createSampler(
		const VkDevice device,
		VkFilter filter,
		VkSamplerAddressMode addressMode,
		float maxLod,
		float maxAnisotropy,
		bool anisotrophyEnable);
}