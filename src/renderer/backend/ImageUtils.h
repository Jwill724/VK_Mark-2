#pragma once

#include "VulkanTypes.h"
#include "../RendererDefinitions.h"
namespace RD = RendererDefinitions;

struct AllocatedImage;
class Swapchain;
enum class ImageAspect;

namespace ImageUtils
{
	// -----------------------------------------------------------------
	// Stages legal on a queue with COMPUTE but not GRAPHICS.
	// TRANSFER family is included: compute queues implicitly support it.
	// DRAW_INDIRECT is legal — it covers vkCmdDispatchIndirect reads.
	// -----------------------------------------------------------------
	constexpr VkPipelineStageFlags2 kComputeLegalStages =
		VK_PIPELINE_STAGE_2_NONE                  |
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT       |
		VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT    |
		VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT      |
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT    |
		VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT     |
		VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT      |
		VK_PIPELINE_STAGE_2_COPY_BIT              |
		VK_PIPELINE_STAGE_2_BLIT_BIT              |
		VK_PIPELINE_STAGE_2_CLEAR_BIT             |
		VK_PIPELINE_STAGE_2_RESOLVE_BIT           |
		VK_PIPELINE_STAGE_2_HOST_BIT;

	// -----------------------------------------------------------------
	// Access flags legal on a compute queue. Attachment access, index
	// and vertex-attribute reads are graphics-only.
	// -----------------------------------------------------------------
	constexpr VkAccessFlags2 kComputeLegalAccess =
		VK_ACCESS_2_NONE                       |
		VK_ACCESS_2_MEMORY_READ_BIT            |
		VK_ACCESS_2_MEMORY_WRITE_BIT           |
		VK_ACCESS_2_SHADER_READ_BIT            |
		VK_ACCESS_2_SHADER_WRITE_BIT           |
		VK_ACCESS_2_SHADER_SAMPLED_READ_BIT    |
		VK_ACCESS_2_SHADER_STORAGE_READ_BIT    |
		VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT   |
		VK_ACCESS_2_UNIFORM_READ_BIT           |
		VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT  |
		VK_ACCESS_2_TRANSFER_READ_BIT          |
		VK_ACCESS_2_TRANSFER_WRITE_BIT         |
		VK_ACCESS_2_HOST_READ_BIT              |
		VK_ACCESS_2_HOST_WRITE_BIT;

	struct FilteredScope
	{
		VkPipelineStageFlags2 stageMask;
		VkAccessFlags2        accessMask;
		VkImageLayout         layout;
		bool                  bWasNarrowed;
	};

	inline FilteredScope FilterScopeForCompute(const ImageBarrierInfo& scope)
	{
		const VkPipelineStageFlags2 stages = scope.stageMask  & kComputeLegalStages;
		const VkAccessFlags2        access = scope.accessMask & kComputeLegalAccess;

		return FilteredScope{
			stages,
			access,
			scope.layout,
			(stages != scope.stageMask) || (access != scope.accessMask)
		};
	};

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

	void TransitionLayoutCompute(
		VkCommandBuffer cmd,
		const AllocatedImage& image,
		RD::ImageAccess oldAccess,
		RD::ImageAccess newAccess,
		uint32_t baseMip   = 0,
		uint32_t mipCount  = VK_REMAINING_MIP_LEVELS);

	void SwapchainPresentCopy(
		VkCommandBuffer cmd,
		const Swapchain& swapchain,
		const AllocatedImage& srcImage);

	void ImageCopyNoBarrier(
		VkCommandBuffer cmd,
		const AllocatedImage& src,
		const AllocatedImage& dst);


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

	VkSampler CreateSamplerAddr(
		VkDevice device,
		VkFilter filter,
		VkSamplerAddressMode addressU,
		VkSamplerAddressMode addressV,
		VkSamplerAddressMode addressW,
		float maxLod,
		float maxAnisotropy,
		VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR);
}
