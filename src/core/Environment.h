#pragma once

#include "core/ResourceManager.h"

namespace Environment {
	constexpr uint32_t CUBEMAP_RESOLUTION{ 1024 };
	constexpr VkExtent3D CUBEMAP_EXTENTS{ CUBEMAP_RESOLUTION, CUBEMAP_RESOLUTION, 1 };
	constexpr uint32_t SPECULAR_PREFILTERED_MIP_LEVELS{ 5 };
	constexpr VkExtent3D DIFFUSE_IRRADIANCE_BASE_EXTENTS{ 32, 32, 1 };
	constexpr float DIFFUSE_SAMPLE_DELTA{ 0.025f };
	constexpr uint32_t PREFILTER_SAMPLE_COUNT{ 1024 };
	constexpr VkExtent3D LUT_IMAGE_EXTENT{ 512, 512, 1 };

	void dispatchEnvironmentMaps(const VkDevice device, GPUResources& resources, ImageTableManager& globalImgTable);
}