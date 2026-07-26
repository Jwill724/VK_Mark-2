#include "pch.h"

#include "ImageUtils.h"
#include "memory/AllocatedImage.h"
#include "../backend/Swapchain.h"

static VkImageAspectFlags ResolveAspectMask(ImageAspect imageAspect)
{
	VkImageAspectFlags aspectMask = (imageAspect == ImageAspect::Color) ?
		VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
	return aspectMask;
}

static void CopyImageToImageDirect(
	VkCommandBuffer cmd,
	VkImage src,
	VkImage dst,
	VkExtent2D extent,
	ImageAspect aspect = ImageAspect::Color)
{
	VkImageAspectFlags aspectMask = ResolveAspectMask(aspect);

	VkImageCopy2 copyRegion{};
	copyRegion.sType = VK_STRUCTURE_TYPE_IMAGE_COPY_2;
	copyRegion.srcSubresource.aspectMask = aspectMask;
	copyRegion.srcSubresource.mipLevel = 0;
	copyRegion.srcSubresource.baseArrayLayer = 0;
	copyRegion.srcSubresource.layerCount = 1;

	copyRegion.dstSubresource.aspectMask = aspectMask;
	copyRegion.dstSubresource.mipLevel = 0;
	copyRegion.dstSubresource.baseArrayLayer = 0;
	copyRegion.dstSubresource.layerCount = 1;

	copyRegion.extent.width = extent.width;
	copyRegion.extent.height = extent.height;
	copyRegion.extent.depth = 1;

	VkCopyImageInfo2 copyInfo{};
	copyInfo.sType = VK_STRUCTURE_TYPE_COPY_IMAGE_INFO_2;
	copyInfo.srcImage = src;
	copyInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	copyInfo.dstImage = dst;
	copyInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	copyInfo.regionCount = 1;
	copyInfo.pRegions = &copyRegion;

	vkCmdCopyImage2(cmd, &copyInfo);
}

static void CopyImageToImageBlit(
	VkCommandBuffer cmd,
	VkImage source,
	VkImage destination,
	VkExtent2D srcSize,
	VkExtent2D dstSize,
	ImageAspect aspect = ImageAspect::Color)
{
	VkImageAspectFlags aspectMask = ResolveAspectMask(aspect);

	VkImageBlit2 blitRegion{};
	blitRegion.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
	blitRegion.pNext = nullptr;

	blitRegion.srcOffsets[1].x = srcSize.width;
	blitRegion.srcOffsets[1].y = srcSize.height;
	blitRegion.srcOffsets[1].z = 1;

	blitRegion.dstOffsets[1].x = dstSize.width;
	blitRegion.dstOffsets[1].y = dstSize.height;
	blitRegion.dstOffsets[1].z = 1;

	blitRegion.srcSubresource.aspectMask = aspectMask;
	blitRegion.srcSubresource.baseArrayLayer = 0;
	blitRegion.srcSubresource.layerCount = 1;
	blitRegion.srcSubresource.mipLevel = 0;

	blitRegion.dstSubresource.aspectMask = aspectMask;
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
	blitInfo.filter = VK_FILTER_NEAREST;
	blitInfo.regionCount = 1;
	blitInfo.pRegions = &blitRegion;

	vkCmdBlitImage2(cmd, &blitInfo);
}

void ImageUtils::TransitionRawImageLayout(
	VkCommandBuffer cmd,
	VkImage image,
	ImageAspect aspect,
	RD::ImageAccess oldAccess,
	RD::ImageAccess newAccess,
	uint32_t baseMip,
	uint32_t mipCount)
{
	const ImageBarrierInfo src = GetImageSyncScope(oldAccess);
	const ImageBarrierInfo dst = GetImageSyncScope(newAccess);

	VkImageMemoryBarrier2 barrier
	{
		VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2
	};

	barrier.srcStageMask  = src.stageMask;
	barrier.srcAccessMask = src.accessMask;

	barrier.dstStageMask  = dst.stageMask;
	barrier.dstAccessMask = dst.accessMask;

	barrier.oldLayout = src.layout;
	barrier.newLayout = dst.layout;

	barrier.image = image;

	barrier.subresourceRange.aspectMask = ResolveAspectMask(aspect);

	barrier.subresourceRange.baseMipLevel = baseMip;
	barrier.subresourceRange.levelCount   = mipCount;

	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount     = VK_REMAINING_ARRAY_LAYERS;

	VkDependencyInfo dep
	{
		VK_STRUCTURE_TYPE_DEPENDENCY_INFO
	};

	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers = &barrier;

	vkCmdPipelineBarrier2(cmd, &dep);
}

void ImageUtils::TransitionLayout(
	VkCommandBuffer cmd,
	const AllocatedImage& img,
	RD::ImageAccess oldAccess,
	RD::ImageAccess newAccess,
	uint32_t baseMip,
	uint32_t mipCount)
{
	const ImageBarrierInfo src = GetImageSyncScope(oldAccess);
	const ImageBarrierInfo dst = GetImageSyncScope(newAccess);

	VkImageMemoryBarrier2 barrier
	{
		VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2
	};

	barrier.srcStageMask  = src.stageMask;
	barrier.srcAccessMask = src.accessMask;

	barrier.dstStageMask  = dst.stageMask;
	barrier.dstAccessMask = dst.accessMask;

	barrier.oldLayout = src.layout;
	barrier.newLayout = dst.layout;

	barrier.image = img.m_image;

	barrier.subresourceRange.aspectMask = ResolveAspectMask(img.m_aspect);

	barrier.subresourceRange.baseMipLevel = baseMip;
	barrier.subresourceRange.levelCount   = mipCount;

	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount     = VK_REMAINING_ARRAY_LAYERS;

	VkDependencyInfo dep
	{
		VK_STRUCTURE_TYPE_DEPENDENCY_INFO
	};

	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers = &barrier;

	vkCmdPipelineBarrier2(cmd, &dep);
}

void ImageUtils::TransitionLayoutCompute(
	VkCommandBuffer cmd,
	const AllocatedImage& img,
	RD::ImageAccess oldAccess,
	RD::ImageAccess newAccess,
	uint32_t baseMip,
	uint32_t mipCount)
{
	const FilteredScope src = FilterScopeForCompute(GetImageSyncScope(oldAccess));
	const FilteredScope dst = FilterScopeForCompute(GetImageSyncScope(newAccess));

	ASSERT(dst.stageMask != VK_PIPELINE_STAGE_2_NONE || newAccess == RD::ImageAccess::Undefined);

	ASSERT(dst.layout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && dst.layout != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	VkImageMemoryBarrier2 barrier
	{
		VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2
	};

	if (src.bWasNarrowed)
	{
		// Cross-queue: the semaphore carries the memory dependency.
		barrier.srcStageMask  = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_NONE;
	}
	else
	{
		// Intra-batch: masks are exact.
		barrier.srcStageMask  = src.stageMask;
		barrier.srcAccessMask = src.accessMask;
	}

	barrier.dstStageMask  = dst.stageMask;
	barrier.dstAccessMask = dst.accessMask;

	barrier.oldLayout = src.layout;
	barrier.newLayout = dst.layout;

	barrier.image = img.m_image;

	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

	barrier.subresourceRange.aspectMask     = ResolveAspectMask(img.m_aspect);
	barrier.subresourceRange.baseMipLevel   = baseMip;
	barrier.subresourceRange.levelCount     = mipCount;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount     = VK_REMAINING_ARRAY_LAYERS;

	VkDependencyInfo dep
	{
		VK_STRUCTURE_TYPE_DEPENDENCY_INFO
	};

	dep.imageMemoryBarrierCount = 1;
	dep.pImageMemoryBarriers = &barrier;

	vkCmdPipelineBarrier2(cmd, &dep);
}

void ImageUtils::ImageCopy(
	VkCommandBuffer cmd,
	const AllocatedImage& src,
	const AllocatedImage& dst,

	RD::ImageAccess srcOldAccess,
	RD::ImageAccess dstOldAccess,

	RD::ImageAccess srcFinalAccess,
	RD::ImageAccess dstFinalAccess,

	bool copyDirect)
{
	TransitionLayout(
		cmd,
		src,
		srcOldAccess,
		RD::ImageAccess::TransferSrc);

	TransitionLayout(
		cmd,
		dst,
		dstOldAccess,
		RD::ImageAccess::TransferDst);

	VkExtent2D extent
	{
		src.Width(),
		src.Height()
	};

	if (copyDirect)
	{
		CopyImageToImageDirect(
			cmd,
			src.m_image,
			dst.m_image,
			extent,
			src.m_aspect);
	}
	else
	{
		ASSERT(src.m_aspect == dst.m_aspect);

		CopyImageToImageBlit(
			cmd,
			src.m_image,
			dst.m_image,
			extent,
			extent,
			src.m_aspect);
	}

	TransitionLayout(
		cmd,
		src,
		RD::ImageAccess::TransferSrc,
		srcFinalAccess);

	TransitionLayout(
		cmd,
		dst,
		RD::ImageAccess::TransferDst,
		dstFinalAccess);
}

void ImageUtils::SwapchainPresentCopy(
	VkCommandBuffer cmd,
	const Swapchain& swapchain,
	const AllocatedImage& srcImage)
{
	VkImage swapImage = swapchain.GetCurrentImage();
	const VkExtent2D swapExtent = swapchain.GetExtent();
 
	TransitionRawImageLayout(
		cmd,
		swapImage,
		ImageAspect::Color,
		RD::ImageAccess::Undefined,
		RD::ImageAccess::TransferDst);
 
	CopyImageToImageBlit(
		cmd,
		srcImage.m_image,
		swapImage,
		{ srcImage.Width(), srcImage.Height() },
		swapExtent,
		ImageAspect::Color);
 
	TransitionRawImageLayout(
		cmd,
		swapImage,
		ImageAspect::Color,
		RD::ImageAccess::TransferDst,
		RD::ImageAccess::Present);
}


void ImageUtils::ImageCopyNoBarrier(
	VkCommandBuffer cmd,
	const AllocatedImage& src,
	const AllocatedImage& dst)
{
	ASSERT(src.m_aspect == dst.m_aspect &&
		"ImageCopyNoBarrier: source and destination aspects differ");
 
	ASSERT(src.Width() == dst.Width() && src.Height() == dst.Height() &&
		"ImageCopyNoBarrier: extents differ — use a blit instead");

	// Assumes src is TRANSFER_SRC_OPTIMAL and dst TRANSFER_DST_OPTIMAL.
	CopyImageToImageBlit(
		cmd,
		src.m_image,
		dst.m_image,
		{ src.Width(), src.Height() },
		{ dst.Width(), dst.Height() },
		src.m_aspect);
}

uint32_t ImageUtils::CalculateMipLevels(uint32_t width, uint32_t height, uint32_t maxMipCap)
{
	if (width == 0 || height == 0) return 0;

	uint32_t size = std::max(width, height);

	uint32_t mipLevels = 1;
	while (size > 1)
	{
		size >>= 1;
		++mipLevels;
	}

	return (maxMipCap > 0)
		? std::min(mipLevels, maxMipCap)
		: mipLevels;
}

void ImageUtils::GenerateMipLevels(VkCommandBuffer cmd, const AllocatedImage& image)
{
	const uint32_t mipLevels  = image.m_mipLevels;
	const uint32_t layerCount = image.m_bIsCubemap ? 6u : 1u;
	VkImage        img        = image.m_image;

	int32_t mipWidth  = static_cast<int32_t>(image.Width());
	int32_t mipHeight = static_cast<int32_t>(image.Height());

	for (uint32_t mip = 1; mip < mipLevels; ++mip)
	{
		// mip-1 is in TRANSFER_DST (either from initial copy or from previous blit dst)
		VkImageMemoryBarrier srcBarrier{};
		srcBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		srcBarrier.image               = img;
		srcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		srcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		srcBarrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 1, 0, layerCount };
		srcBarrier.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		srcBarrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		srcBarrier.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
		srcBarrier.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;

		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &srcBarrier);

		int32_t dstWidth  = std::max(mipWidth  / 2, 1);
		int32_t dstHeight = std::max(mipHeight / 2, 1);

		VkImageBlit blit{};
		blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 0, layerCount };
		blit.srcOffsets[1]  = { mipWidth, mipHeight, 1 };
		blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, layerCount };
		blit.dstOffsets[1]  = { dstWidth, dstHeight, 1 };

		vkCmdBlitImage(cmd,
			img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blit, VK_FILTER_LINEAR);

		// mip-1 done as source — transition to SHADER_READ
		VkImageMemoryBarrier readBarrier  = srcBarrier;
		readBarrier.oldLayout             = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		readBarrier.newLayout             = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		readBarrier.srcAccessMask         = VK_ACCESS_TRANSFER_READ_BIT;
		readBarrier.dstAccessMask         = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(cmd,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &readBarrier);

		mipWidth  = dstWidth;
		mipHeight = dstHeight;
	}

	// Last mip is still in TRANSFER_DST from the blit destination
	VkImageMemoryBarrier lastMip{};
	lastMip.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	lastMip.image               = img;
	lastMip.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	lastMip.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	lastMip.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, mipLevels - 1, 1, 0, layerCount };
	lastMip.oldLayout           = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	lastMip.newLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	lastMip.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
	lastMip.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier(cmd,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &lastMip);
}

void ImageUtils::GenerateCubemapMipLevels(VkCommandBuffer cmd, const AllocatedImage& image)
{
	const uint32_t mipLevels = image.m_mipLevels;
	VkImage img = image.m_image;

	for (uint32_t face = 0; face < 6; ++face)
	{
		int32_t mipWidth  = image.Width();
		int32_t mipHeight = image.Height();

		for (uint32_t mip = 1; mip < mipLevels; ++mip)
		{
			VkImageMemoryBarrier srcBarrier{};
			srcBarrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			srcBarrier.image               = img;
			srcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			srcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			srcBarrier.subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, 1, face, 1 };
			srcBarrier.oldLayout           = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			srcBarrier.newLayout           = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			srcBarrier.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
			srcBarrier.dstAccessMask       = VK_ACCESS_TRANSFER_READ_BIT;

			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &srcBarrier);

			VkImageMemoryBarrier dstBarrier = srcBarrier;
			dstBarrier.subresourceRange.baseMipLevel = mip;
			dstBarrier.oldLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			dstBarrier.newLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			dstBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
			dstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				0, 0, nullptr, 0, nullptr, 1, &dstBarrier);

			int32_t dstWidth  = std::max(mipWidth  / 2, 1);
			int32_t dstHeight = std::max(mipHeight / 2, 1);

			VkImageBlit blit{};
			blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip - 1, face, 1 };
			blit.srcOffsets[1]  = { mipWidth, mipHeight, 1 };
			blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, mip, face, 1 };
			blit.dstOffsets[1]  = { dstWidth, dstHeight, 1 };

			vkCmdBlitImage(cmd,
				img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1, &blit, VK_FILTER_LINEAR);

			VkImageMemoryBarrier finalBarriers[2]{};
			finalBarriers[0]               = srcBarrier;
			finalBarriers[0].oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			finalBarriers[0].newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			finalBarriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			finalBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			finalBarriers[1]               = dstBarrier;
			finalBarriers[1].oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			finalBarriers[1].newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			finalBarriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			finalBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(cmd,
				VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				0, 0, nullptr, 0, nullptr, 2, finalBarriers);

			mipWidth  = dstWidth;
			mipHeight = dstHeight;
		}
	}
}

VkImageView ImageUtils::CreateImageView(
	VkDevice device,
	VkImage image,
	VkFormat format,
	VkImageAspectFlags aspectFlags,
	uint32_t mipLevels)
{
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

void ImageUtils::DestroyImageView(VkDevice device, VkImageView view)
{
	if (view != VK_NULL_HANDLE)
		vkDestroyImageView(device, view, nullptr);
}


VkSampler ImageUtils::CreateSampler(
	VkDevice device,
	VkFilter filter,
	VkSamplerAddressMode addressMode,
	float maxLod,
	float maxAnisotropy,
	VkSamplerMipmapMode mipmapMode,
	bool compareEnabled,
	VkBorderColor borderColor)
{
	VkSamplerCreateInfo samplerInfo{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	samplerInfo.mipmapMode = mipmapMode;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = maxLod;
	samplerInfo.magFilter = filter;
	samplerInfo.minFilter = filter;
	samplerInfo.mipLodBias = 0.0f;

	// Default no anisotropy filtering
	samplerInfo.anisotropyEnable = VK_FALSE;
	samplerInfo.maxAnisotropy = 1.0f;

	if (maxAnisotropy > 1.0f)
	{
		samplerInfo.anisotropyEnable = VK_TRUE;
		samplerInfo.maxAnisotropy = maxAnisotropy;
	}

	samplerInfo.addressModeU = addressMode;
	samplerInfo.addressModeV = addressMode;
	samplerInfo.addressModeW = addressMode;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.borderColor = borderColor;

	// Default no sampler compare
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

	if (compareEnabled)
	{
		samplerInfo.compareEnable = VK_TRUE;
		samplerInfo.compareOp = VK_COMPARE_OP_LESS;
	}

	VkSampler sampler;
	VK_CHECK(vkCreateSampler(device, &samplerInfo, nullptr, &sampler));
	return sampler;
}

size_t ImageUtils::GetPixelSize(VkFormat format)
{
	if (format == 0)
	{
		ASSERT(format != 0 && "Invalid VkFormat type!");
		return 0;
	}

	switch (format)
	{
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

		// 3-channel 8-bit formats
		case VK_FORMAT_R8G8B8_UNORM:
		case VK_FORMAT_R8G8B8_SRGB:
		case VK_FORMAT_B8G8R8_UNORM:
		case VK_FORMAT_B8G8R8_SRGB:
			return 3;

		// 4-channel 8-bit formats
		case VK_FORMAT_R8G8B8A8_UNORM:
		case VK_FORMAT_R8G8B8A8_SNORM:
		case VK_FORMAT_R8G8B8A8_UINT:
		case VK_FORMAT_R8G8B8A8_SINT:
		case VK_FORMAT_R8G8B8A8_SRGB:
		case VK_FORMAT_B8G8R8A8_UNORM:
		case VK_FORMAT_B8G8R8A8_SRGB:
			return 4;

		// === 16-bit formats ===
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

		// === 32-bit float/int formats ===G
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

		// Packed formats
		case VK_FORMAT_B10G11R11_UFLOAT_PACK32:
			return 4;

		case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
		case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
		case VK_FORMAT_A2B10G10R10_UINT_PACK32:
		case VK_FORMAT_A2R10G10B10_UINT_PACK32:
			return 4;

		case VK_FORMAT_D32_SFLOAT:
			return 4;

		default:
			ASSERT(false && "Unhandled VkFormat in getPixelSize");
			return 0;
	}
}
