#pragma once

#include "ResourceTypes.h"
#include "Mesh.h"
#include "Material.h"
#include "../renderer/scene/World.h"
#include "../renderer/backend/memory/TextureStaging.h"

// -----------------------------------------------------------------------
// Texture upload descriptor — produced by DecodeImages
// Raw pixel data owned here until Renderer stages it
// -----------------------------------------------------------------------
struct TextureDesc
{
	std::vector<uint8_t>        pixelData;
	std::vector<TextureMipDesc> mips;

	uint32_t bindlessID = UINT32_MAX;

	uint32_t      width = 0;
	uint32_t      height = 0;
	TextureFormat format = TextureFormat::RGBA8;
	bool          isSRGB = false;
	std::string   debugName;

	bool IsValid() const noexcept { return !pixelData.empty() && !mips.empty(); }
};

// -----------------------------------------------------------------------
// Sampler descriptor — produced by BuildSamplers
// No VkSampler here — Renderer creates and owns it
// -----------------------------------------------------------------------
struct SamplerDesc
{
	bool   isLinear     = true;
	bool   isMipMapped  = true;
	float  anisotropy   = 16.0f;

	// Filled by Renderer after creation
	uint32_t rendererSlot = UINT32_MAX; // index into scene's sampler list
};

// -----------------------------------------------------------------------
// Material descriptor — produced by ProcessMaterials
// References texture/sampler by local scene index, not GPU handle
// -----------------------------------------------------------------------
struct MaterialDesc
{
	// Local indices into TextureDesc array for this scene
	// UINT32_MAX = use fallback
	uint32_t albedoTexIdx       = UINT32_MAX;
	uint32_t albedoSamplerIdx   = UINT32_MAX;
	uint32_t metalRoughTexIdx   = UINT32_MAX;
	uint32_t metalRoughSampIdx  = UINT32_MAX;
	uint32_t normalTexIdx       = UINT32_MAX;
	uint32_t normalSamplerIdx   = UINT32_MAX;
	uint32_t emissiveTexIdx     = UINT32_MAX;
	uint32_t emissiveSampIdx    = UINT32_MAX;

	glm::vec4  colorFactor      = { 1, 1, 1, 1 };
	glm::vec2  metalRoughFactors= { 0, 1 };
	glm::vec3  emissiveColor    = { 0, 0, 0 };
	float      emissiveStrength = 1.0f;
	float      alphaCutoff      = 0.5f;
	float      normalScale      = 1.0f;
	float      ior              = 1.5f;
	float      specularFactor   = 1.0f;
	float      clearcoatFactor = 0.0f;
	float      clearcoatRough = 0.0f;
	glm::vec3  sheenColor = glm::vec3(0.0f);
	float      sheenRough = 0.0f;
	float      transmissionFactor = 0.0f;
	float      diffuseTransFactor = 0.0f;
	float      thicknessFactor = 0.0f;
	glm::vec3  attenuationColor = glm::vec3(1.0f);
	float      attenuationDistance = 0.0f;

	uint32_t   shadingModel = UINT32_MAX;

	uint32_t   flags            = 0;
	uint32_t   passType         = 0; // MaterialPass enum cast

	// Filled by Renderer after GPU material buffer write
	uint32_t   globalMaterialID = UINT32_MAX;
};

// -----------------------------------------------------------------------
// Mesh descriptor — produced by ProcessMeshes
// Vertex/index data is referenced by offset into the scene's flat buffers
// -----------------------------------------------------------------------
struct MeshDesc
{
	AABB localAABB;
	float localBoundingRadius            = 0.0f;
	uint32_t firstIndex                  = UINT32_MAX;
	uint32_t indexCount                  = UINT32_MAX;
	uint32_t vertexOffset                = UINT32_MAX;
	uint32_t vertexCount                 = UINT32_MAX;
	uint32_t meshletOffset               = 0;
	uint32_t meshletCount                = 0;
	uint32_t meshletVisibilityBase       = 0;
	uint32_t shadowFirstIndex            = UINT32_MAX;
	uint32_t shadowIndexCount            = UINT32_MAX;
	uint32_t shadowMeshletOffset         = 0;
	uint32_t shadowMeshletCount          = 0;

	// LOD mesh indices into the scene's mesh desc array (UINT32_MAX = fallback to lod0)
	uint32_t lod0 = UINT32_MAX;
	uint32_t lod1 = UINT32_MAX;
	uint32_t lod2 = UINT32_MAX;
	uint32_t lod3 = UINT32_MAX;

	uint32_t shadowLod0 = UINT32_MAX;
	uint32_t shadowLod1 = UINT32_MAX;
	uint32_t shadowLod2 = UINT32_MAX;

	uint32_t flags = 0;

	// Filled by Renderer after GPU mesh buffer write
	uint32_t globalMeshID = UINT32_MAX;
};

struct SceneLightDesc
{
	glm::vec3 position = { 0, 0, 0 };
	float     range = 0.0f;

	glm::vec3 direction = { 0, 0, -1 };
	float     intensity = 1.0f;

	glm::vec3 color = { 1, 1, 1 };
	float     innerCos = 1.0f;

	float     outerCos = 0.0f;
	uint32_t  type = 0;  // 0 point, 1 spot
};

// -----------------------------------------------------------------------
// Instance descriptor — one primitive in the scene
// -----------------------------------------------------------------------
struct InstanceDesc
{
	uint32_t localMeshIdx     = UINT32_MAX; // index into MeshDesc array
	uint32_t localMaterialIdx = UINT32_MAX; // index into MaterialDesc array
	uint32_t nodeIdx          = UINT32_MAX; // which node owns this primitive
	uint32_t passType         = 0;
};

// Lightweight post-upload record.
// AssetManager fills SceneUploadBatch during pipeline.
// Renderer writes bindless IDs back here after GPU upload.
// World holds shared_ptr<ModelAsset> for scene lifetime.
struct ModelAsset
{
	ModelID     sceneID   = ModelID::Count;
	std::string sceneName;

	// Written by Renderer::UploadScene after GPU allocation.
	// One entry per TextureDesc in the original batch.
	// UINT32_MAX = was invalid, fallback to checkerboard.
	std::vector<uint32_t> textureBindlessIDs;

	// One entry per MaterialDesc — index into GPU material buffer.
	std::vector<uint32_t> materialGlobalIDs;

	// One entry per MeshDesc — index into MeshRegistry.
	std::vector<uint32_t> meshGlobalIDs;

	// Slots owned by this asset in BindlessImageTable::m_assetTextures.
	// Handed back to Renderer on unload for refcount decrement / free.
	std::vector<uint32_t> ownedTextureSlots;

	// Node slot mapping — built by StageBuildSceneGraph, consumed by Visibility
	// primitive i -> which slot in the transform slab it uses
	std::vector<uint32_t> localToNodeSlot;

	// Scene graph — used by World for transform integration.
	std::vector<glm::mat4> nodeTransforms;

	// Instancing metadata — integrated into World::globalInstances on load.
	VirtualInstance virtualInstance{};

	// Instance draw data — consumed by World/Visibility to build draw lists.
	// One per primitive, same order as SceneUploadBatch::instances.
	std::vector<InstanceDesc> instances;

	std::vector<SceneLightDesc> lights;
	std::vector<uint32_t>       lightIDs;  // filled by World, used on unload

	RD::ResourceLifetime lifetime = RD::ResourceLifetime::Asset;

	bool IsLoaded() const noexcept { return !meshGlobalIDs.empty(); }
};


// -----------------------------------------------------------------------
// Full scene upload batch — AssetManager fills this, Renderer consumes it
// AssetManager hands off ownership of pixel data here
// -----------------------------------------------------------------------
struct SceneUploadBatch
{
	ModelID  sceneID = ModelID::Count;
	std::string sceneName;

	// Flat CPU-side geometry — Renderer uploads these in one staging pass
	std::vector<Vertex>   vertices;
	std::vector<uint32_t> indices;
	std::vector<Meshlet>  meshlets;
	std::vector<uint32_t> meshletVertices;
	std::vector<uint8_t>  meshletTriangles;

	// Per-scene resource descriptors
	std::vector<TextureDesc>  textures;  // owns pixel data until Renderer stages
	std::vector<SamplerDesc>  samplers;
	std::vector<MaterialDesc> materials;
	std::vector<MeshDesc>     meshes;
	std::vector<InstanceDesc> instances;
	std::vector<uint32_t> materialFlags;

	std::vector<SceneLightDesc> lights;
	std::vector<glm::mat4> lightTransforms;

	// Node transforms for scene graph
	std::vector<glm::mat4> nodeTransforms;
	std::vector<uint32_t> localToNodeSlot;

	// Virtual instance metadata (scene-level instancing info)
	VirtualInstance virtualInstance{};

	// Lifetime hint from AssetManager -> Renderer
	RD::ResourceLifetime lifetime = RD::ResourceLifetime::Asset;

	bool IsComplete() const noexcept
	{
		return !vertices.empty()
			&& !indices.empty()
			&& !meshes.empty();
	}
};
