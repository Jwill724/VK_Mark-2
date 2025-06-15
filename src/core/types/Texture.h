#pragma once

#include "common/Vk_Types.h"
#include "common/EngineTypes.h"
#include <fastgltf/types.hpp>
#include "common/ResourceTypes.h"

namespace Textures {
	std::optional<AllocatedImage> loadImage(fastgltf::Asset& asset, fastgltf::Image& image,
		VkFormat format, ThreadContext& ctx, std::filesystem::path basePath, const VmaAllocator allocator, DeletionQueue& bufferQueue);
	void generateMipmaps(VkCommandBuffer cmd, const AllocatedImage& image);
	VkSampler createSampler(VkFilter filter, VkSamplerAddressMode addressMode, float maxLod, float maxAnisotropy, bool anisotrophyEnable);
	VkFilter extract_filter(fastgltf::Filter filter);
	VkSamplerMipmapMode extract_mipmap_mode(fastgltf::Filter filter);
}