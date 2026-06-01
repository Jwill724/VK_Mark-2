#pragma once

#include "VulkanTypes.h"
#include "../RendererDefinitions.h"
namespace RD = RendererDefinitions;

struct AllocatedImage;
class Swapchain;
enum class ImageAspect;

namespace ImageUtils
{
	void TransitionLayout(
		VkCommandBuffer cmd,
		const AllocatedImage& img,
		RD::ImageAccess oldAccess,
		RD::ImageAccess newAccess,
		uint32_t baseMip = 0,                          // Starting mip
		uint32_t mipCount = VK_REMAINING_MIP_LEVELS);  // How many levels transitioned
	void TransitionRawImageLayout(
		VkCommandBuffer cmd,
		VkImage image,
		ImageAspect aspect,
		RD::ImageAccess oldAccess,
		RD::ImageAccess newAccess,
		uint32_t baseMip = 0,
		uint32_t mipCount = VK_REMAINING_MIP_LEVELS);
	void ImageCopy(
		VkCommandBuffer cmd,
		const AllocatedImage& src,
		const AllocatedImage& dst,

		RD::ImageAccess srcOldAccess,
		RD::ImageAccess dstOldAccess,

		RD::ImageAccess srcFinalAccess,
		RD::ImageAccess dstFinalAccess,

		bool copyDirect = true); // Turn false if formats don't match

	void SwapchainPresentCopy(
		VkCommandBuffer cmd,
		const Swapchain& swapchain,
		const AllocatedImage& srcImage);

	uint32_t CalculateMipLevels(uint32_t width, uint32_t height, uint32_t maxMipCap = UINT32_MAX);

	void GenerateMipLevels(VkCommandBuffer cmd, const AllocatedImage& image);
	void GenerateCubemapMipLevels(VkCommandBuffer cmd, const AllocatedImage& image);
	size_t GetPixelSize(VkFormat format);
	VkImageView CreateImageView(
		VkDevice device,
		VkImage image,
		VkFormat format,
		VkImageAspectFlags aspectFlags,
		uint32_t mipLevels);
	void DestroyImageView(VkDevice device, VkImageView view);
	VkSampler CreateSampler(
		VkDevice device,
		VkFilter filter,
		VkSamplerAddressMode addressMode,
		float maxLod,
		float maxAnisotropy,
		VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
		bool compareEnabled = false,
		VkBorderColor borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK);
}
