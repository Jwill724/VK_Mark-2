#pragma once

#include <cstdint>
#include <glm/glm.hpp>

// GPU material flags
inline constexpr uint32_t MATERIAL_FLAG_ALPHA_MASKED   = 1u << 0;
inline constexpr uint32_t MATERIAL_FLAG_CASTS_SHADOWS  = 1u << 1;
inline constexpr uint32_t MATERIAL_FLAG_HAS_NORMAL_MAP = 1u << 2;
inline constexpr uint32_t MATERIAL_FLAG_IS_TREE        = 1u << 3;
inline constexpr uint32_t MATERIAL_FLAG_DOUBLE_SIDED   = 1u << 4;
inline constexpr uint32_t MATERIAL_FLAG_TRANSMISSIVE   = 1u << 5;

inline constexpr uint32_t SHADING_MODEL_STANDARD       = 0u;
inline constexpr uint32_t SHADING_MODEL_CLEARCOAT      = 1u;
inline constexpr uint32_t SHADING_MODEL_SHEEN          = 2u;
inline constexpr uint32_t SHADING_MODEL_TRANSMISSION   = 3u;
inline constexpr uint32_t SHADING_MODEL_DIFFUSE_TRANS  = 4u;

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

	float ior = 1.5f;
	float specularFactor = 1.0f;

	float clearcoatFactor = 0.0f;
	float clearcoatRough = 0.0f;

	glm::vec3 sheenColor = glm::vec3(0.0f);
	float sheenRough = 0.0f;

	float transmissionFactor = 0.0f;
	float diffuseTransFactor = 0.0f;

	float      thicknessFactor = 0.0f;
	glm::vec3  attenuationColor = glm::vec3(1.0f);
	float      attenuationDistance = 0.0f;

	uint32_t shadingModel = SHADING_MODEL_STANDARD;

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

static float SrgbToLinear(uint8_t v)
{
	const float c = v / 255.0f;
	return (c <= 0.04045f) ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

static uint8_t LinearToSrgb(float c)
{
	c = glm::clamp(c, 0.0f, 1.0f);
	const float s = (c <= 0.0031308f) ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
	return static_cast<uint8_t>(s * 255.0f + 0.5f);
}

static void DownsampleBox(
	const std::vector<uint8_t>& src, uint32_t sw, uint32_t sh, bool isSRGB,
	std::vector<uint8_t>& dst, uint32_t dw, uint32_t dh)
{
	dst.resize(static_cast<size_t>(dw) * dh * 4u);

	for (uint32_t y = 0; y < dh; ++y)
		for (uint32_t x = 0; x < dw; ++x)
		{
			const uint32_t x0 = std::min(x * 2u, sw - 1u);
			const uint32_t x1 = std::min(x * 2u + 1u, sw - 1u);
			const uint32_t y0 = std::min(y * 2u, sh - 1u);
			const uint32_t y1 = std::min(y * 2u + 1u, sh - 1u);

			const size_t s[4] = {
				(static_cast<size_t>(y0) * sw + x0) * 4u,
				(static_cast<size_t>(y0) * sw + x1) * 4u,
				(static_cast<size_t>(y1) * sw + x0) * 4u,
				(static_cast<size_t>(y1) * sw + x1) * 4u
			};

			const size_t d = (static_cast<size_t>(y) * dw + x) * 4u;

			for (uint32_t c = 0; c < 3; ++c)
			{
				if (isSRGB)
				{
					const float sum = SrgbToLinear(src[s[0] + c]) + SrgbToLinear(src[s[1] + c])
						+ SrgbToLinear(src[s[2] + c]) + SrgbToLinear(src[s[3] + c]);
					dst[d + c] = LinearToSrgb(sum * 0.25f);
				}
				else
				{
					const uint32_t sum = src[s[0] + c] + src[s[1] + c] + src[s[2] + c] + src[s[3] + c];
					dst[d + c] = static_cast<uint8_t>((sum + 2u) / 4u);
				}
			}

			const uint32_t a = src[s[0] + 3] + src[s[1] + 3] + src[s[2] + 3] + src[s[3] + 3];
			dst[d + 3] = static_cast<uint8_t>((a + 2u) / 4u);
		}
}
