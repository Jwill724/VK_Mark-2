#pragma once

#include <cstdint>
#include <glm/glm.hpp>

// GPU material flags
inline constexpr uint32_t MATERIAL_FLAG_ALPHA_MASKED   = 1u << 0;
inline constexpr uint32_t MATERIAL_FLAG_CASTS_SHADOWS  = 1u << 1;
inline constexpr uint32_t MATERIAL_FLAG_HAS_NORMAL_MAP = 1u << 2;
inline constexpr uint32_t MATERIAL_FLAG_IS_TREE        = 1u << 3;
inline constexpr uint32_t MATERIAL_FLAG_DOUBLE_SIDED   = 1u << 4;

// Every scene emits a default material at this slot in ProcessMaterials.
inline constexpr uint32_t DEFAULT_MATERIAL_INDEX = 0u;

enum class MaterialPass
{
	Opaque,
	Transparent
};

struct Material
{
	glm::vec4 colorFactor = glm::vec4(1.0f);

	glm::vec2 metalRoughFactors = glm::vec2(1.0f, 1.0f);
	float normalScale = 1.0f;
	float alphaCutoff = 1.0f;

	glm::vec3 emissiveColor = glm::vec3(0.0f);
	float emissiveStrength = 1.0f;

	uint32_t albedoID = UINT32_MAX;
	uint32_t metalRoughnessID = UINT32_MAX;
	uint32_t normalID = UINT32_MAX;
	uint32_t emissiveID = UINT32_MAX;
};

enum class MaterialType
{
	Unknown,
	Albedo,
	Normal,
	MetalRoughness,
	Emissive
};
