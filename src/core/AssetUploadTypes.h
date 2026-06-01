#pragma once

//#include <vector>
//#include <string>
//#include <cstdint>
//#include "ResourceTypes.h"
//#include "Vertex.h"
//#include "../renderer/scene/World.h"
//
//// -----------------------------------------------------------------------
//// Texture upload descriptor — produced by DecodeImages
//// Raw pixel data owned here until Renderer stages it
//// -----------------------------------------------------------------------
//struct TextureDesc
//{
//	std::vector<uint8_t> pixelData;   // stbi decoded, always RGBA8
//	uint32_t             width       = 0;
//	uint32_t             height      = 0;
//	bool                 isSRGB      = false;
//	bool                 needsMips   = false; // width >= 8 && height >= 8
//	std::string          debugName;
//
//	// Filled by Renderer after GPU allocation — asset metadata reads this back
//	uint32_t             bindlessID  = UINT32_MAX;
//
//	size_t ByteSize()    const noexcept { return pixelData.size(); }
//	size_t PixelBytes()  const noexcept { return 4u; } // always RGBA8 from stbi
//	bool   IsValid()     const noexcept { return !pixelData.empty() && width > 0 && height > 0; }
//};
//
//// -----------------------------------------------------------------------
//// Sampler descriptor — produced by BuildSamplers
//// No VkSampler here — Renderer creates and owns it
//// -----------------------------------------------------------------------
//struct SamplerDesc
//{
//	bool   isLinear     = true;
//	bool   isMipMapped  = true;
//	float  anisotropy   = 16.0f;
//
//	// Filled by Renderer after creation
//	uint32_t rendererSlot = UINT32_MAX; // index into scene's sampler list
//};
//
//// -----------------------------------------------------------------------
//// Material descriptor — produced by ProcessMaterials
//// References texture/sampler by local scene index, not GPU handle
//// -----------------------------------------------------------------------
//struct MaterialDesc
//{
//	// Local indices into TextureDesc array for this scene
//	// UINT32_MAX = use fallback
//	uint32_t albedoTexIdx       = UINT32_MAX;
//	uint32_t albedoSamplerIdx   = UINT32_MAX;
//	uint32_t metalRoughTexIdx   = UINT32_MAX;
//	uint32_t metalRoughSampIdx  = UINT32_MAX;
//	uint32_t normalTexIdx       = UINT32_MAX;
//	uint32_t normalSamplerIdx   = UINT32_MAX;
//	uint32_t emissiveTexIdx     = UINT32_MAX;
//	uint32_t emissiveSampIdx    = UINT32_MAX;
//
//	glm::vec4  colorFactor      = { 1, 1, 1, 1 };
//	glm::vec2  metalRoughFactors= { 0, 1 };
//	glm::vec3  emissiveColor    = { 0, 0, 0 };
//	float      emissiveStrength = 1.0f;
//	float      alphaCutoff      = 0.5f;
//	float      normalScale      = 1.0f;
//
//	uint32_t   flags            = 0;
//	uint32_t   passType         = 0; // MaterialPass enum cast
//
//	// Filled by Renderer after GPU material buffer write
//	uint32_t   globalMaterialID = UINT32_MAX;
//};
//
//// -----------------------------------------------------------------------
//// Mesh descriptor — produced by ProcessMeshes
//// Vertex/index data is referenced by offset into the scene's flat buffers
//// -----------------------------------------------------------------------
//struct MeshDesc
//{
//	uint32_t firstIndex    = 0;
//	uint32_t indexCount    = 0;
//	uint32_t vertexOffset  = 0;
//	uint32_t vertexCount   = 0;
//
//	struct AABB
//	{
//		glm::vec3 vmin{ 0 };
//		glm::vec3 vmax{ 0 };
//	} localAABB;
//
//	// LOD mesh indices into the scene's mesh desc array (UINT32_MAX = fallback to lod0)
//	uint32_t lod0 = UINT32_MAX;
//	uint32_t lod1 = UINT32_MAX;
//	uint32_t lod2 = UINT32_MAX;
//	uint32_t lod3 = UINT32_MAX;
//
//	uint32_t shadowLod0 = UINT32_MAX;
//	uint32_t shadowLod1 = UINT32_MAX;
//	uint32_t shadowLod2 = UINT32_MAX;
//
//	uint32_t flags = 0;
//
//	// Filled by Renderer after GPU mesh buffer write
//	uint32_t globalMeshID = UINT32_MAX;
//};
//
//// -----------------------------------------------------------------------
//// Instance descriptor — one primitive in the scene
//// -----------------------------------------------------------------------
//struct InstanceDesc
//{
//	uint32_t localMeshIdx     = UINT32_MAX; // index into MeshDesc array
//	uint32_t localMaterialIdx = UINT32_MAX; // index into MaterialDesc array
//	uint32_t nodeIdx          = UINT32_MAX; // which node owns this primitive
//	uint32_t passType         = 0;
//};
//
//// -----------------------------------------------------------------------
//// Full scene upload batch — AssetManager fills this, Renderer consumes it
//// AssetManager hands off ownership of pixel data here
//// -----------------------------------------------------------------------
//struct SceneUploadBatch
//{
//	ModelID  sceneID   = ModelID::Count;
//	std::string sceneName;
//
//	// Flat CPU-side geometry — Renderer uploads these in one staging pass
//	std::vector<Vertex>    vertices;
//	std::vector<uint32_t>  indices;
//
//	// Per-scene resource descriptors
//	std::vector<TextureDesc>  textures;  // owns pixel data until Renderer stages
//	std::vector<SamplerDesc>  samplers;
//	std::vector<MaterialDesc> materials;
//	std::vector<MeshDesc>     meshes;
//	std::vector<InstanceDesc> instances;
//
//	// Node transforms for scene graph
//	std::vector<glm::mat4>   nodeTransforms;
//
//	// Virtual instance metadata (scene-level instancing info)
//	VirtualInstance virtualInstance{};
//
//	// Lifetime hint from AssetManager → Renderer
//	RD::ResourceLifetime lifetime = RD::ResourceLifetime::Asset;
//
//	bool IsComplete() const noexcept
//	{
//		return !vertices.empty()
//			&& !indices.empty()
//			&& !meshes.empty();
//	}
//};
