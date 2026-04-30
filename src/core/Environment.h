#pragma once

#include "core/ResourceManager.h"

namespace Environment
{
	constexpr VkExtent3D SKYBOX_EXTENTS{ 512, 512, 1 };
	constexpr VkExtent3D SPECULAR_EXTENTS{ 256, 256, 1 };
	constexpr uint32_t SPECULAR_PREFILTERED_MIP_LEVELS{ 9 };
	constexpr VkExtent3D DIFFUSE_IRRADIANCE_BASE_EXTENTS{ 32, 32, 1 };
	constexpr float DIFFUSE_SAMPLE_DELTA{ 0.025f };
	constexpr uint32_t PREFILTER_SAMPLE_COUNT{ 2048 };
	constexpr VkExtent3D LUT_IMAGE_EXTENT{ 256, 256, 1 };

	void dispatchEnvironmentMaps(
		const VkDevice device,
		GPUResources& resources);
}
