#include "pch.h"

#include "RendererUtils.h"
#include "vulkan/Backend.h"
#include "renderer/Renderer.h"
#include "core/types/Texture.h"
#include "common/EngineConstants.h"
#include "renderer/gpu_types/CommandBuffer.h"
#include "utils/BufferUtils.h"
#include "core/EngineState.h"

VkSemaphore RendererUtils::createSemaphore() {
	static std::mutex mutex;
	std::scoped_lock lock(mutex);

	VkSemaphoreCreateInfo semaphoreInfo{};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkSemaphore semaphore;
	VK_CHECK(vkCreateSemaphore(Backend::getDevice(), &semaphoreInfo, nullptr, &semaphore));

	return semaphore;
}

void RendererUtils::createTimelineSemaphore(TimelineSync& sync) {
	static std::mutex mutex;
	std::scoped_lock lock(mutex);
	VkSemaphoreTypeCreateInfo timelineCreateInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
		.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
		.initialValue = 0
	};

	VkSemaphoreCreateInfo createInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = &timelineCreateInfo
	};

	VK_CHECK(vkCreateSemaphore(Backend::getDevice(), &createInfo, nullptr, &sync.semaphore));
	sync.signalValue = 1;
}

VkFence RendererUtils::createFence() {
	static std::mutex mutex;
	std::scoped_lock lock(mutex);

	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	VkFence fence;
	VK_CHECK(vkCreateFence(Backend::getDevice(), &fenceInfo, nullptr, &fence));

	return fence;
}

// TODO: When I get a better image loading library I'll rework this to be able to create large staging buffers for many textures into a single cmd

void RendererUtils::createTextureImage(
	VkCommandPool cmdPool,
	void* data,
	AllocatedImage& renderImage,
	VkImageUsageFlags usage,
	VkSampleCountFlagBits samples,
	DeletionQueue& imageQueue,
	DeletionQueue& bufferQueue,
	const VmaAllocator allocator,
	bool skipQueueUsage)
{
	size_t pixelBytes = getPixelSize(renderImage.imageFormat);

	size_t dataSize = static_cast<size_t>(renderImage.imageExtent.width) * renderImage.imageExtent.height * pixelBytes;

	AllocatedBuffer uploadBuffer = BufferUtils::createBuffer(dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, allocator);
	memcpy(uploadBuffer.info.pMappedData, data, dataSize);

	createRenderImage(renderImage, usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		samples, imageQueue, allocator, skipQueueUsage);

	CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {
		RendererUtils::transitionImage(
			cmd,
			renderImage.image,
			renderImage.imageFormat,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		VkBufferImageCopy copyRegion{};
		copyRegion.bufferOffset = 0;
		copyRegion.bufferRowLength = 0;
		copyRegion.bufferImageHeight = 0;
		copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copyRegion.imageSubresource.mipLevel = 0;
		copyRegion.imageSubresource.baseArrayLayer = 0;
		copyRegion.imageSubresource.layerCount = 1;
		copyRegion.imageExtent = renderImage.imageExtent;

		vkCmdCopyBufferToImage(cmd,
			uploadBuffer.buffer,
			renderImage.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&copyRegion);

		if (renderImage.mipmapped) {
			Textures::generateMipmaps(cmd, renderImage);
		}
		else {
			RendererUtils::transitionImage(
				cmd,
				renderImage.image,
				renderImage.imageFormat,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}
	}, cmdPool, QueueType::Graphics);

	auto buffer = uploadBuffer.buffer;
	auto bufAlloc = uploadBuffer.allocation;
	auto alloc = allocator;
	bufferQueue.push_function([buffer, bufAlloc, alloc] {
		BufferUtils::destroyBuffer(buffer, bufAlloc, alloc);
	});
}

// TODO: Rework image system to handle new additions to AllocatedImage struct,
// this should scale toward a render pass and graph system.
void RendererUtils::createRenderImage(AllocatedImage& renderImage, VkImageUsageFlags usage,
	VkSampleCountFlagBits samples, DeletionQueue& dq, const VmaAllocator alloc, bool skipDQ) {
	static std::mutex imageMutex;

	auto device = Backend::getDevice();

	VkImageCreateInfo imgInfo{};
	imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imgInfo.imageType = VK_IMAGE_TYPE_2D;
	imgInfo.extent = renderImage.imageExtent;
	imgInfo.format = renderImage.imageFormat;
	imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imgInfo.usage = usage;
	imgInfo.samples = samples;
	imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	// enable mip-maps to get levels per extent of image
	if (renderImage.mipmapped && renderImage.mipLevelCount == 0) {
		imgInfo.mipLevels = calculateMipLevels(renderImage);
		renderImage.mipLevelCount = imgInfo.mipLevels;
	}
	// mip count already predefined
	else if (renderImage.mipLevelCount > 0) {
		imgInfo.mipLevels = renderImage.mipLevelCount;
	}
	else {
		imgInfo.mipLevels = 1;
		renderImage.mipLevelCount = 1;
	}

	if (renderImage.isCubeMap) {
		imgInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		imgInfo.arrayLayers = 6;
		renderImage.arrayLayers = 6;
	}
	else {
		imgInfo.arrayLayers = 1;
		renderImage.arrayLayers = 1;
	}

	VmaAllocationCreateInfo imgAllocInfo{};
	imgAllocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
	imgAllocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

	{
		std::scoped_lock lock(imageMutex);
		VK_CHECK(vmaCreateImage(alloc, &imgInfo, &imgAllocInfo, &renderImage.image, &renderImage.allocation, nullptr));

		// sampled view creation
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = renderImage.image;
		viewInfo.format = renderImage.imageFormat;
		viewInfo.subresourceRange.aspectMask = (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
			? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = imgInfo.mipLevels;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = renderImage.arrayLayers;
		viewInfo.viewType = renderImage.isCubeMap ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;

		VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &renderImage.imageView));

		// storage view creation
		if (usage & VK_IMAGE_USAGE_STORAGE_BIT && samples == VK_SAMPLE_COUNT_1_BIT) {
			viewInfo.viewType = (imgInfo.arrayLayers > 1 || renderImage.isCubeMap)
				? VK_IMAGE_VIEW_TYPE_2D_ARRAY
				: VK_IMAGE_VIEW_TYPE_2D;

			VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &renderImage.storageView));

			if (imgInfo.mipLevels > 1 && renderImage.isCubeMap && renderImage.perMipStorageViews) {
				renderImage.storageViews.resize(imgInfo.mipLevels);

				for (uint32_t mip = 0; mip < imgInfo.mipLevels; ++mip) {
					VkImageViewCreateInfo mipViewInfo = viewInfo;
					mipViewInfo.subresourceRange.baseMipLevel = mip;
					mipViewInfo.subresourceRange.layerCount = 6;
					mipViewInfo.subresourceRange.levelCount = 1;

					VK_CHECK(vkCreateImageView(device, &mipViewInfo, nullptr, &renderImage.storageViews[mip]));
				}
			}
		}
	}

	if (!skipDQ) {
		auto image = renderImage.image;
		auto imgAlloc = renderImage.allocation;
		auto imgView = renderImage.imageView;
		auto& v_storageViews = renderImage.storageViews;
		auto storageView = renderImage.storageView;

		// the deletion queue needs copies don't try to add destroyImage since it takes it by reference
		dq.push_function([device, image, alloc, imgView, storageView, v_storageViews, imgAlloc] {
			if (imgView != VK_NULL_HANDLE)
				vkDestroyImageView(device, imgView, nullptr);
			if (storageView != VK_NULL_HANDLE)
				vkDestroyImageView(device, storageView, nullptr);

			for (auto& view : v_storageViews) {
				if (view != VK_NULL_HANDLE)
					vkDestroyImageView(device, view, nullptr);
			}

			if (image != VK_NULL_HANDLE)
				vmaDestroyImage(alloc, image, imgAlloc);
		});
	}
}

void RendererUtils::destroyImage(VkDevice device, AllocatedImage& img, const VmaAllocator allocator) {
	static std::mutex imageMutex;
	std::scoped_lock lock(imageMutex);
	if (img.imageView != VK_NULL_HANDLE)
		vkDestroyImageView(device, img.imageView, nullptr);

	if (img.storageView != VK_NULL_HANDLE)
		vkDestroyImageView(device, img.storageView, nullptr);

	for (auto& view : img.storageViews) {
		if (view != VK_NULL_HANDLE)
			vkDestroyImageView(device, view, nullptr);
	}

	if (img.image != VK_NULL_HANDLE && img.allocation != nullptr)
		vmaDestroyImage(allocator, img.image, img.allocation);
}


void RendererUtils::transitionImage(
	VkCommandBuffer cmd, VkImage image, VkFormat format,
	VkImageLayout oldLayout, VkImageLayout newLayout,
	VkPipelineStageFlags2 dstStageOverride,
	VkAccessFlags2        dstAccessOverride)
{
	VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
	b.oldLayout = oldLayout;
	b.newLayout = newLayout;
	b.image = image;

	VkImageAspectFlags aspect = 0;
	if (format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_X8_D24_UNORM_PACK32 ||
		format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_S8_UINT ||
		format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
		if (VulkanUtils::hasStencilComponent(format)) aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
		aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
	}
	else {
		aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	}
	b.subresourceRange = { aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };

	// src (based on oldLayout)
	switch (oldLayout) {
	case VK_IMAGE_LAYOUT_UNDEFINED:
		b.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
		b.srcAccessMask = 0;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
		b.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
		b.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		b.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
		break;
	case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
		b.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		b.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL:
	case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
		// Fix 2: correct producer for depth
		b.srcStageMask = VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
		b.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		break;
	case VK_IMAGE_LAYOUT_GENERAL:
		// typical producer is compute
		b.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
		b.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT;
		break;
	case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
		b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		b.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
		break;
	default:
		b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		b.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
		break;
	}

	// dst (based on newLayout) — allow override for compute/fragment
	if (dstStageOverride) { // explicit control
		b.dstStageMask = dstStageOverride;
		b.dstAccessMask = dstAccessOverride;
	}
	else {
		switch (newLayout) {
		case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
			b.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			b.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
			b.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			b.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
			b.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			b.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
			break;
		case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
			b.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
				VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
			b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_GENERAL:
			b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
			b.dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT;
			break;
		case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
			b.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
			b.dstAccessMask = VK_ACCESS_2_NONE;
			break;
		default:
			b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			b.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;
			break;
		}
	}

	VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers = &b;
	vkCmdPipelineBarrier2(cmd, &dep);
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
	blitInfo.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
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

uint32_t RendererUtils::calculateMipLevels(AllocatedImage& img, uint32_t maxMipCap) {
	uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(img.imageExtent.width, img.imageExtent.height)))) + 1;
	return std::min(mipLevels, maxMipCap);
}

void RendererUtils::generateCubemapMiplevels(VkCommandBuffer cmd, AllocatedImage& image) {
	assert(image.isCubeMap && "generateCubemapMiplevels requires a cubemap image");

	const uint32_t mipLevels = image.mipLevelCount;
	const uint32_t faceCount = 6;
	VkImage img = image.image;

	for (uint32_t face = 0; face < faceCount; ++face) {
		int32_t mipWidth = static_cast<int32_t>(image.imageExtent.width);
		int32_t mipHeight = static_cast<int32_t>(image.imageExtent.height);

		for (uint32_t mip = 1; mip < mipLevels; ++mip) {
			//fmt::print("    Blitting mip {} -> {}\n", mip - 1, mip);
			// Transition source mip level to TRANSFER_SRC_OPTIMAL
			VkImageMemoryBarrier srcBarrier{};
			srcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			srcBarrier.image = img;
			srcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			srcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			srcBarrier.subresourceRange = {
				VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 1, face, 1
			};
			srcBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			srcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			srcBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			srcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &srcBarrier);

			// Transition destination mip level to TRANSFER_DST_OPTIMAL
			VkImageMemoryBarrier dstBarrier = srcBarrier;
			dstBarrier.subresourceRange.baseMipLevel = mip;
			dstBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			dstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			dstBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			dstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &dstBarrier);

			// Blit from mip-1 to mip
			VkImageBlit blit{};
			blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, face, 1 };
			blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };

			int32_t dstWidth = std::max(mipWidth / 2, 1);
			int32_t dstHeight = std::max(mipHeight / 2, 1);

			blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, face, 1 };
			blit.dstOffsets[1] = { dstWidth, dstHeight, 1 };

			vkCmdBlitImage(cmd,
				img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &blit, VK_FILTER_LINEAR);

			// Transition both mips to SHADER_READ_ONLY
			VkImageMemoryBarrier finalBarriers[2]{};

			finalBarriers[0] = srcBarrier;
			finalBarriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			finalBarriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			finalBarriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			finalBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			finalBarriers[1] = dstBarrier;
			finalBarriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			finalBarriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			finalBarriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			finalBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 2, finalBarriers);

			mipWidth = dstWidth;
			mipHeight = dstHeight;

			//fmt::print("    Mip {} size: {}x{}\n", mip, mipWidth, mipHeight);
		}
	}
}

VkImageView RendererUtils::createImageView(VkDevice device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels) {
	static std::mutex viewMutex;
	std::scoped_lock lock(viewMutex);

	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = aspectFlags;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = mipLevels;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	VkImageView imageView;
	VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &imageView));

	return imageView;
}

// bytes-per-channel (float) * channels-per-pixel
size_t RendererUtils::getPixelSize(VkFormat format) {
	if (format == 0) {
		ASSERT(format != 0 && "Invalid VkFormat type!");
		return 0;
	}

	switch (format) {
		// 8-bit formats
	case VK_FORMAT_R8_UNORM:
	case VK_FORMAT_R8_SNORM:
	case VK_FORMAT_R8_UINT:
	case VK_FORMAT_R8_SINT:
		return 1;

		// 2-channel 8-bit formats
	case VK_FORMAT_R8G8_UNORM:
	case VK_FORMAT_R8G8_SNORM:
	case VK_FORMAT_R8G8_UINT:
	case VK_FORMAT_R8G8_SINT:
		return 2;

		// 4-channel 8-bit formats
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_R8G8B8A8_SNORM:
	case VK_FORMAT_R8G8B8A8_UINT:
	case VK_FORMAT_R8G8B8A8_SINT:
	case VK_FORMAT_R8G8B8A8_SRGB:
		return 4;

		// 3-channel 8-bit formats (rarely used)
	case VK_FORMAT_R8G8B8_UNORM:
	case VK_FORMAT_R8G8B8_SRGB:
	case VK_FORMAT_B8G8R8_UNORM:
	case VK_FORMAT_B8G8R8_SRGB:
		return 3;

		// 4-channel BGRA formats
	case VK_FORMAT_B8G8R8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_SRGB:
		return 4;

		// 16-bit formats
	case VK_FORMAT_R16_UNORM:
	case VK_FORMAT_R16_SNORM:
	case VK_FORMAT_R16_UINT:
	case VK_FORMAT_R16_SINT:
	case VK_FORMAT_R16_SFLOAT:
		return 2;

	case VK_FORMAT_R16G16_UNORM:
	case VK_FORMAT_R16G16_SNORM:
	case VK_FORMAT_R16G16_UINT:
	case VK_FORMAT_R16G16_SINT:
	case VK_FORMAT_R16G16_SFLOAT:
		return 4;

	case VK_FORMAT_R16G16B16A16_UNORM:
	case VK_FORMAT_R16G16B16A16_SNORM:
	case VK_FORMAT_R16G16B16A16_UINT:
	case VK_FORMAT_R16G16B16A16_SINT:
	case VK_FORMAT_R16G16B16A16_SFLOAT:
		return 8;

		// 32-bit float/int formats
	case VK_FORMAT_R32_UINT:
	case VK_FORMAT_R32_SINT:
	case VK_FORMAT_R32_SFLOAT:
		return 4;

	case VK_FORMAT_R32G32_UINT:
	case VK_FORMAT_R32G32_SINT:
	case VK_FORMAT_R32G32_SFLOAT:
		return 8;

	case VK_FORMAT_R32G32B32_UINT:
	case VK_FORMAT_R32G32B32_SINT:
	case VK_FORMAT_R32G32B32_SFLOAT:
		return 12;

	case VK_FORMAT_R32G32B32A32_UINT:
	case VK_FORMAT_R32G32B32A32_SINT:
	case VK_FORMAT_R32G32B32A32_SFLOAT:
		return 16;

	default:
		ASSERT(false && "Unhandled VkFormat in getPixelSize");
		return 0;
	}
}

static inline void resolveFamilies(uint32_t& src, uint32_t& dst, bool concurrent) {
	// If buffer is concurrent OR families match, do NOT encode a QFOT.
	if (concurrent || src == dst) {
		src = VK_QUEUE_FAMILY_IGNORED;
		dst = VK_QUEUE_FAMILY_IGNORED;
	}
}

uint32_t RendererUtils::queueFamilyIndex(QueueType q) {
	switch (q) {
	case QueueType::Graphics: return Backend::getGraphicsQueue().familyIndex;
	case QueueType::Transfer: return Backend::getTransferQueue().familyIndex;
	case QueueType::Compute:  return Backend::getComputeQueue().familyIndex;
	default: ASSERT(false && "queueFamilyIndex: unknown QueueType"); return VK_QUEUE_FAMILY_IGNORED;
	}
}

void RendererUtils::releaseBuffer(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	VkPipelineStageFlags2 srcStage,
	VkAccessFlags2        srcAccess,
	uint32_t              srcFamily,
	uint32_t              dstFamily)
{
	uint32_t s = srcFamily, d = dstFamily;
	resolveFamilies(s, d, buf.isConcurrent);

	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask = srcStage;
	b.srcAccessMask = srcAccess;
	b.dstStageMask = VK_PIPELINE_STAGE_2_NONE; // release
	b.dstAccessMask = 0;
	b.srcQueueFamilyIndex = s;
	b.dstQueueFamilyIndex = d;
	b.buffer = buf.buffer;
	b.offset = 0;
	b.size = VK_WHOLE_SIZE;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers = &b;
	vkCmdPipelineBarrier2(cmd, &di);
}

void RendererUtils::acquireBuffer(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	VkPipelineStageFlags2  dstStage,
	VkAccessFlags2         dstAccess,
	uint32_t               srcFamily,
	uint32_t               dstFamily)
{
	uint32_t s = srcFamily, d = dstFamily;
	resolveFamilies(s, d, buf.isConcurrent);

	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask = VK_PIPELINE_STAGE_2_NONE; // acquire
	b.srcAccessMask = 0;
	b.dstStageMask = dstStage;
	b.dstAccessMask = dstAccess;
	b.srcQueueFamilyIndex = s;
	b.dstQueueFamilyIndex = d;
	b.buffer = buf.buffer;
	b.offset = 0;
	b.size = VK_WHOLE_SIZE;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers = &b;
	vkCmdPipelineBarrier2(cmd, &di);
}

void RendererUtils::releaseBufferQ(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	VkPipelineStageFlags2  srcStage,
	VkAccessFlags2         srcAccess,
	QueueType              srcQ,
	QueueType              dstQ)
{
	releaseBuffer(cmd, buf, srcStage, srcAccess,
		queueFamilyIndex(srcQ), queueFamilyIndex(dstQ));
}

void RendererUtils::acquireBufferQ(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	VkPipelineStageFlags2  dstStage,
	VkAccessFlags2         dstAccess,
	QueueType              srcQ,
	QueueType              dstQ)
{
	acquireBuffer(cmd, buf, dstStage, dstAccess,
		queueFamilyIndex(srcQ), queueFamilyIndex(dstQ));
}

// transfer -> shader read
void RendererUtils::releaseTransferToShaderReadQ(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	QueueType srcQ,
	QueueType dstQ)
{
	releaseBufferQ(cmd, buf,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		VK_ACCESS_2_TRANSFER_WRITE_BIT,
		srcQ, dstQ);
}

void RendererUtils::acquireShaderReadQ(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	QueueType srcQ,
	QueueType dstQ)
{
	acquireBufferQ(cmd, buf,
		VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT,
		srcQ, dstQ);
}

// transfer -> indirect
void RendererUtils::releaseTransferToIndirectQ(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	QueueType srcQ,
	QueueType dstQ)
{
	releaseBufferQ(cmd, buf,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		VK_ACCESS_2_TRANSFER_WRITE_BIT,
		srcQ, dstQ);
}

void RendererUtils::acquireIndirectQ(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	QueueType srcQ,
	QueueType dstQ)
{
	acquireBufferQ(cmd, buf,
		VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
		VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
		srcQ, dstQ);
}

// transfer -> vertex/index
void RendererUtils::releaseTransferToVertexIndexQ(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	QueueType srcQ,
	QueueType dstQ)
{
	releaseBufferQ(cmd, buf,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		VK_ACCESS_2_TRANSFER_WRITE_BIT,
		srcQ, dstQ);
}

void RendererUtils::acquireVertexIndexQ(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	QueueType srcQ,
	QueueType dstQ)
{
	acquireBufferQ(cmd, buf,
		VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
		VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT,
		srcQ, dstQ);
}

// compute producers
void RendererUtils::releaseComputeWriteQ(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	QueueType srcQ,
	QueueType dstQ)
{
	releaseBufferQ(cmd, buf,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		VK_ACCESS_2_SHADER_WRITE_BIT,
		srcQ, dstQ);
}

void RendererUtils::releaseComputeToIndirectQ(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	QueueType srcQ,
	QueueType dstQ)
{
	releaseBufferQ(cmd, buf,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		VK_ACCESS_2_SHADER_WRITE_BIT, // compute fills indirect args
		srcQ, dstQ);
}