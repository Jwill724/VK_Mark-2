#pragma once

#include <renderer/RendererDefinitions.h>
#include <renderer/backend/memory/VmaForward.h>
#include <vector>
#include <vulkan/vulkan.h>

struct AllocatedImage
{
	VkImage     image     = VK_NULL_HANDLE;
	VkImageView imageView = VK_NULL_HANDLE;

	// Storage views — only Allocator/ImageUtils touch these
	std::vector<VkImageView> storageViews{};
	std::vector<VkImageView> layerViews{};
	bool bPerMipStorageViews            = false;

	VkFormat              format        = VK_FORMAT_UNDEFINED;
	VkExtent3D            extent        {};
	VkImageLayout         currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkSampleCountFlagBits samples       = VK_SAMPLE_COUNT_1_BIT;
	VkImageType           imageType     = VK_IMAGE_TYPE_2D;
	VkImageViewType       viewType      = VK_IMAGE_VIEW_TYPE_2D;

	uint32_t mipLevelCount = 0;
	uint32_t arrayLayers   = 1;

	VmaAllocation allocation = VK_NULL_HANDLE;

	uint32_t bindlessID = UINT32_MAX;

	RendererDefinitions::ResourceLifetime lifetime = RendererDefinitions::ResourceLifetime::Persistent;

	bool bIsMipmapped = false;
	bool bIsCubemap = false;

	bool IsValid() const noexcept { return image != VK_NULL_HANDLE; }
	void Reset()   { *this = AllocatedImage{}; }
};
