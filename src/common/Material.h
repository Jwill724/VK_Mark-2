#pragma once

#include <Core.h>

// GPU material flags
inline constexpr uint32_t MATERIAL_FLAG_ALPHA_MASKED   = 1u << 0;
inline constexpr uint32_t MATERIAL_FLAG_CASTS_SHADOWS  = 1u << 1;
inline constexpr uint32_t MATERIAL_FLAG_HAS_NORMAL_MAP = 1u << 2;
inline constexpr uint32_t MATERIAL_FLAG_IS_TREE        = 1u << 3;

enum class MaterialPass
{
	Opaque,
	Transparent
};

struct Material
{
	glm::vec4 colorFactor = glm::vec4(1.0f);
	glm::vec2 metalRoughFactors = glm::vec2(1.0f, 1.0f);

	glm::vec3 emissiveColor = glm::vec3(0.0f);
	float emissiveStrength = 1.0f;

	float normalScale = 1.0f;
	float alphaCutoff = 1.0f;
	uint32_t passType = 0;
	uint32_t flags = 0;

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


static VkFormat getImageFormatFromName(const std::string& imageName)
{
	bool isSRGB =
		imageName.find("_BaseColor") != std::string::npos ||
		imageName.find("_Albedo") != std::string::npos ||
		imageName.find("diffuse") != std::string::npos ||
		imageName.find("_Emissive") != std::string::npos ||
		imageName.find("emissive") != std::string::npos;

	return isSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
}

inline void SetRuntimeImageSemantic(
	ModelAsset::GPUData& runtime,
	uint32_t imageIndex,
	MaterialType semantic)
{
	ASSERT(imageIndex < runtime.images.size());

	RuntimeImage& runtimeImage = runtime.images[imageIndex];

	if (runtimeImage.semantic == MaterialType::Unknown) {
		runtimeImage.semantic = semantic;
		return;
	}

	if (runtimeImage.semantic != semantic) {
		fmt::println(
			"[AssetManager] image semantic conflict at image index {}. Existing: {}, Incoming: {}",
			imageIndex,
			static_cast<uint32_t>(runtimeImage.semantic),
			static_cast<uint32_t>(semantic)
		);
	}
}

static const char* textureSemanticToString(MaterialType semantic)
{
	switch (semantic)
	{
	case MaterialType::Unknown:        return "Unknown";
	case MaterialType::Albedo:         return "Albedo";
	case MaterialType::Normal:         return "Normal";
	case MaterialType::MetalRoughness: return "MetalRoughness";
	case MaterialType::Emissive:       return "Emissive";
	default:                              return "Invalid";
	}
}

// Add this helper near the top of AssetManager.cpp (or in a utils file)
static bool isTreeMaterial(const fastgltf::Material& mat, const fastgltf::Asset& gltf)
{
	if (!mat.name.empty()) {
		std::string lowerName(mat.name.begin(), mat.name.end());

		// Convert to lowercase for matching
		std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
			[](unsigned char c) { return std::tolower(c); });

		if (lowerName.find("tree") != std::string::npos/* ||
			lowerName.find("leaf") != std::string::npos ||
			lowerName.find("foliage") != std::string::npos ||
			lowerName.find("branch") != std::string::npos ||
			lowerName.find("pine") != std::string::npos ||
			lowerName.find("palm") != std::string::npos ||
			lowerName.find("bark") != std::string::npos*/) {
			return true;
		}
	}

	return false;
}

