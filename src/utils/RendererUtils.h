#pragma once

#include "Engine.h"

namespace RendererUtils {
	VkSemaphore createSemaphore();
	VkFence createFence();
	void createDynamicRenderImage(AllocatedImage& renderImage, VkImageUsageFlags usage,
		VkMemoryPropertyFlags properties, VkSampleCountFlagBits samples, DeletionQueue& deletionQueue, VmaAllocator allocator);
	void transitionImage(VkCommandBuffer cmd, VkImage image, VkFormat format, VkImageLayout currentLayout, VkImageLayout newLayout);
	void copyImageToImage(VkCommandBuffer cmd, VkImage source, VkImage destination, VkExtent2D srcSize, VkExtent2D dstSize);
}