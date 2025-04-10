#pragma once

#include "common/Vk_Types.h"
#include <fastgltf/types.hpp>

namespace Textures {
	std::optional<AllocatedImage> loadImage(fastgltf::Asset& asset, fastgltf::Image& image);
	void generateMipmaps(VkCommandBuffer cmd, AllocatedImage image);
}