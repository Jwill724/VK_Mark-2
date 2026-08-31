#pragma once

#include "AssetUploadTypes.h"
#include "Vertex.h"
#include <filesystem>

struct SourceImage
{
	std::vector<uint8_t>  encodedBytes;
	std::filesystem::path filePath;
	std::string           name;
	bool                  isSRGB = false;
	bool                  isHeightMap = false;
	float                 bumpStrength = 1.0f;
	bool                  isNormalMap = false;

	std::filesystem::path alphaMaskPath;
	bool                  isGlossMap = false;
};
struct SourcePrimitive
{
	std::vector<Vertex>   vertices;
	std::vector<uint32_t> indices;
	uint32_t              materialIdx = DEFAULT_MATERIAL_INDEX;
};

struct SourceNode
{
	std::vector<uint32_t>        transformIndices;
	std::vector<SourcePrimitive> primitives;
};

struct SourceLight
{
	glm::vec3 color = { 1, 1, 1 };
	float     intensity = 1.0f;
	float     range = 0.0f;
	float     innerConeAngle = 0.0f;
	float     outerConeAngle = 0.7853982f;
	uint32_t  type = 0;
	uint32_t  transformIndex = UINT32_MAX;
};

struct ImportOptions
{
	float importScale = 1.0f;
	float lightIntensityScale = 1.0f;
	bool  flipUVs = false;
	bool useCache = true;
};

struct SourceScene
{
	std::vector<glm::mat4>    transforms;
	std::vector<SourceImage>  images;
	std::vector<SamplerDesc>  samplers;
	std::vector<MaterialDesc> materials;
	std::vector<SourceNode>   nodes;
	std::vector<SourceLight>  lights;

	uint32_t AddTransform(const glm::mat4& m)
	{
		const uint32_t idx = static_cast<uint32_t>(transforms.size());
		transforms.push_back(m);
		return idx;
	}

	bool IsValid() const noexcept { return !nodes.empty() && !materials.empty(); }
};

bool ImportScene(const std::filesystem::path& file, const ImportOptions& opts, SourceScene& out);
