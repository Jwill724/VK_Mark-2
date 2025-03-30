#include "pch.h"

#include "RendererUtils.h"
#include "vulkan/Backend.h"
#include "renderer/Renderer.h"

VkSemaphore RendererUtils::createSemaphore() {
	VkDevice device = Backend::getDevice();

	VkSemaphoreCreateInfo semaphoreInfo{};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkSemaphore semaphore;
	VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore));

	return semaphore;
}

VkFence RendererUtils::createFence() {
	VkDevice device = Backend::getDevice();

	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	VkFence fence;
	VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &fence));

	return fence;
}

void RendererUtils::createDynamicRenderImage(AllocatedImage& renderImage, VkImageUsageFlags usage,
	VkMemoryPropertyFlags properties, VkSampleCountFlagBits samples, DeletionQueue& deletionQueue, VmaAllocator allocator) {

	VkImageCreateInfo rimg_imageInfo{};
	rimg_imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	rimg_imageInfo.imageType = VK_IMAGE_TYPE_2D;
	rimg_imageInfo.extent = renderImage.imageExtent;
	rimg_imageInfo.arrayLayers = 1;
	rimg_imageInfo.mipLevels = 1;
	rimg_imageInfo.format = renderImage.imageFormat;
	rimg_imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	rimg_imageInfo.usage = usage;
	rimg_imageInfo.samples = samples;
	rimg_imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	rimg_imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo rimg_allocInfo = {};
	rimg_allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	rimg_allocInfo.requiredFlags = properties;

	VK_CHECK(vmaCreateImage(allocator, &rimg_imageInfo, &rimg_allocInfo, &renderImage.image, &renderImage.allocation, nullptr));

	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.image = renderImage.image;
	viewInfo.format = renderImage.imageFormat;
	viewInfo.subresourceRange.aspectMask = (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.layerCount = 1;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.baseArrayLayer = 0;

	VK_CHECK(vkCreateImageView(Backend::getDevice(), &viewInfo, nullptr, &renderImage.imageView));

	auto* imageView = &renderImage.imageView;
	auto* image = &renderImage.image;

	deletionQueue.push_function([=]() {
		vkDestroyImageView(Backend::getDevice(), renderImage.imageView, nullptr);
		*imageView = VK_NULL_HANDLE;
		vmaDestroyImage(allocator, renderImage.image, renderImage.allocation);
		*image = VK_NULL_HANDLE;
	});
}

void RendererUtils::transitionImage(VkCommandBuffer cmd, VkImage image, VkFormat format, VkImageLayout currentLayout, VkImageLayout newLayout) {
	VkImageMemoryBarrier2 imageBarrier{};
	imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	imageBarrier.pNext = nullptr;

	imageBarrier.oldLayout = currentLayout;
	imageBarrier.newLayout = newLayout;
	imageBarrier.image = image;

	// Handle depth/stencil formats properly
	VkImageAspectFlags aspectMask = 0;
	if (newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL ||
		newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {

		aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

		if (VulkanUtils::hasStencilComponent(format)) {
			aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
	}
	else {
		aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

		imageBarrier.subresourceRange.aspectMask = aspectMask;
		imageBarrier.subresourceRange.baseMipLevel = 0;
		imageBarrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
		imageBarrier.subresourceRange.baseArrayLayer = 0;
		imageBarrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

		//imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		//imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
		//imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		//imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

		// Select the correct pipeline stages and access masks based on layouts
		switch (currentLayout) {
		case VK_IMAGE_LAYOUT_UNDEFINED:
			imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
			imageBarrier.srcAccessMask = 0; // No dependencies
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			imageBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			imageBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			imageBarrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			imageBarrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
			break;
		default:
			imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
			break;
		}

		switch (newLayout) {
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			imageBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			imageBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			imageBarrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			imageBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
			imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
			imageBarrier.dstAccessMask = VK_ACCESS_2_NONE; // No need for shader access
			break;
		default:
			imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
			break;
		}

		VkDependencyInfo depInfo{};
		depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		depInfo.pNext = nullptr;
		depInfo.imageMemoryBarrierCount = 1;
		depInfo.pImageMemoryBarriers = &imageBarrier;

		vkCmdPipelineBarrier2(cmd, &depInfo);
	}
}

void RendererUtils::copyImageToImage(VkCommandBuffer cmd, VkImage source, VkImage destination, VkExtent2D srcSize, VkExtent2D dstSize) {
	VkImageBlit2 blitRegion{};
	blitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
	blitRegion.pNext = nullptr;

	blitRegion.srcOffsets[1].x = srcSize.width;
	blitRegion.srcOffsets[1].y = srcSize.height;
	blitRegion.srcOffsets[1].z = 1;

	blitRegion.dstOffsets[1].x = dstSize.width;
	blitRegion.dstOffsets[1].y = dstSize.height;
	blitRegion.dstOffsets[1].z = 1;

	blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.srcSubresource.baseArrayLayer = 0;
	blitRegion.srcSubresource.layerCount = 1;
	blitRegion.srcSubresource.mipLevel = 0;

	blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.dstSubresource.baseArrayLayer = 0;
	blitRegion.dstSubresource.layerCount = 1;
	blitRegion.dstSubresource.mipLevel = 0;

	VkBlitImageInfo2 blitInfo{};
	blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2,
	blitInfo.pNext = nullptr;

	blitInfo.dstImage = destination;
	blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	blitInfo.srcImage = source;
	blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	blitInfo.filter = VK_FILTER_LINEAR;
	blitInfo.regionCount = 1;
	blitInfo.pRegions = &blitRegion;

	vkCmdBlitImage2(cmd, &blitInfo);
}