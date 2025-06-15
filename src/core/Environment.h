#pragma once

#include "common/Vk_Types.h"
#include "common/EngineTypes.h"
#include "core/ResourceManager.h"

namespace Environment {
	constexpr uint32_t CUBEMAP_RESOLUTION{ 1024 };
	constexpr VkExtent3D CUBEMAP_EXTENTS{ CUBEMAP_RESOLUTION, CUBEMAP_RESOLUTION, 1 };
	constexpr uint32_t MAX_ENVIRONMENT_MAPS{ 10 };
	constexpr uint32_t SPECULAR_SAMPLE_COUNT{ 2048 };
	constexpr uint32_t SPECULAR_PREFILTERED_MIP_LEVELS{ 5 };
	constexpr VkExtent3D DIFFUSE_IRRADIANCE_BASE_EXTENTS{ 32, 32, 1 };
	constexpr float DIFFUSE_SAMPLE_DELTA{ 0.025f };
	constexpr uint32_t DIFFUSE_IRRADIANCE_MIP_LEVELS{ 2 };
	constexpr VkExtent3D LUT_IMAGE_EXTENT{ 512, 512, 1 };

	// Asset manager calls both
	void dispatchEnvironmentMaps(GPUResources& resources, ImageTable& imageTable);
}