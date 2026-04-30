#pragma once

#include "renderer/RendererDefinitions.h"
#include <glm/fwd.hpp>
#include "Core.h"
#include "core/Logging.h"

namespace RD = RendererDefinitions;

struct ImageLUTEntry
{
	uint32_t combinedImageIndex = UINT32_MAX;
	uint32_t samplerCubeIndex = UINT32_MAX;

	// Not in use
	uint32_t storageImageIndex = UINT32_MAX;

	static constexpr ImageLUTEntry CombinedOnly(uint32_t id) noexcept {
		return ImageLUTEntry{ .combinedImageIndex = id };
	}
	static constexpr ImageLUTEntry SamplerCubeOnly(uint32_t id) noexcept {
		return ImageLUTEntry{ .samplerCubeIndex = id };
	}

	static constexpr ImageLUTEntry StorageOnly(uint32_t id) noexcept {
		return ImageLUTEntry{ .storageImageIndex = id };
	}

	void Reset() noexcept
	{
		combinedImageIndex = UINT32_MAX;
		samplerCubeIndex = UINT32_MAX;
		storageImageIndex = UINT32_MAX;
	}
};

// Stores indices and ensures only unique valid images are written
class ImageLUTManager final
{
public:
	const std::vector<ImageLUTEntry>& GetEntries() const { return m_entries; }

	void AddCombinedID(uint32_t id)
	{
		std::scoped_lock lock(m_mutex);
		if (id != UINT32_MAX && m_pushedCombined.insert(id).second)
			m_entries.emplace_back(ImageLUTEntry::CombinedOnly(id));
	}

	void AddCubeID(uint32_t id)
	{
		std::scoped_lock lock(m_mutex);
		if (id != UINT32_MAX && m_pushedCube.insert(id).second)
			m_entries.emplace_back(ImageLUTEntry::SamplerCubeOnly(id));
	}

	void Clear()
	{
		std::scoped_lock lock(m_mutex);
		m_entries.clear();
		m_pushedCombined.clear();
		m_pushedCube.clear();
	}

	~ImageLUTManager() { Clear(); }

private:
	std::mutex m_mutex;
	std::vector<ImageLUTEntry> m_entries;
	std::unordered_set<uint32_t> m_pushedCombined;
	std::unordered_set<uint32_t> m_pushedCube;
};

// Descriptor arrays setup
class BindlessImageTable final
{
public:
	using ImageViewSamplerKey = std::pair<VkImageView, VkSampler>;

	struct HashPair
	{
		size_t operator()(const ImageViewSamplerKey& key) const
		{
			return std::hash<std::uintptr_t>()(reinterpret_cast<std::uintptr_t>(key.first)) ^
				(std::hash<std::uintptr_t>()(reinterpret_cast<std::uintptr_t>(key.second)) << 1);
		}
	};

	struct EqualPair
	{
		bool operator()(const ImageViewSamplerKey& a, const ImageViewSamplerKey& b) const noexcept
		{
			return a.first == b.first && a.second == b.second;
		}
	};

	ImageViewSamplerKey MakeKey(VkImageView view, VkSampler sampler)
	{
		return { view, sampler };
	}

	uint32_t PushCombined(
		VkImageView view,
		VkSampler sampler,
		uint32_t threadID = UINT32_MAX,
		Logging& logger)
	{
		std::scoped_lock lock(m_combinedMutex);
		ASSERT(view && sampler && "Null handle in PushCombined");
		auto key = MakeKey(view, sampler);

		if (auto it = m_combinedViewHashToID.find(key); it != m_combinedViewHashToID.end())
			return it->second;

		VkDescriptorImageInfo info { sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		uint32_t index = static_cast<uint32_t>(m_combinedViews.size());
		m_combinedViews.push_back(info);
		m_combinedViewHashToID[key] = index;

		if (ENABLE_DEBUG_LOGS)
		{
			if (threadID == UINT32_MAX)
			{
				fmt::println("[BindlessImageTablePushCombined] New [{}] = view={}, sampler={}", index, (void*)view, (void*)sampler);
			}
			else
			{
				logger.Log(threadID,
					fmt::format("[BindlessImageTable:PushCombined] New [{}] = view={}, sampler={}\n", index, (void*)view, (void*)sampler));
			}
		}

		return index;
	}
	uint32_t PushSamplerCube(
		VkImageView view,
		VkSampler sampler,
		uint32_t threadID = UINT32_MAX,
		Logging* logger = nullptr)
	{
		std::scoped_lock lock(m_samplerCubeMutex);
		ASSERT(view && sampler && "Null handle in PushSamplerCube");
		auto key = MakeKey(view, sampler);

		if (auto it = m_samplerCubeViewHashToID.find(key); it != m_samplerCubeViewHashToID.end())
			return it->second;

		VkDescriptorImageInfo info { sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		uint32_t index = static_cast<uint32_t>(m_samplerCubeViews.size());
		m_samplerCubeViews.push_back(info);
		m_samplerCubeViewHashToID[key] = index;

		if (ENABLE_DEBUG_LOGS)
		{
			if (threadID == UINT32_MAX)
			{
				fmt::print("[ImageTable::PushSamplerCube] New [{}] = view={}, sampler={}\n", index, (void*)view, (void*)sampler);
			}
			else
			{
				logger->Log(threadID,
					fmt::format("[ImageTable::PushSamplerCube] New [{}] = view={}, sampler={}\n", index, (void*)view, (void*)sampler));
			}
		}
		return index;
	}

	//const VkDescriptorImageInfo& GetSamplerCubeDescriptorInfo(uint32_t index) const
	//{
	//	ASSERT(index >= 0 && index < m_samplerCubeViews.size());
	//	return m_samplerCubeViews[index];
	//}
	//const VkDescriptorImageInfo& GetCombinedSamplerDescriptorInfo(uint32_t index) const
	//{
	//	ASSERT(index >= 0 && index < m_combinedViews.size());
	//	return m_combinedViews[index];
	//}

	//uint32_t GetSamplerCubeVectorSize() const noexcept { return m_samplerCubeViews.size(); }
	//uint32_t GetCombinedSamplerVectorSize() const noexcept { return m_combinedViews.size(); }

	const std::vector<VkDescriptorImageInfo>& GetCombinedSamplerDescriptorInfo_v() const { return m_combinedViews; }
	const std::vector<VkDescriptorImageInfo>& GetSamplerCubeDescriptorInfo_v() const { return m_samplerCubeViews; }

	void ClearTables()
	{
		std::scoped_lock l1(m_combinedMutex, m_samplerCubeMutex);
		m_combinedViews.clear();
		m_combinedViewHashToID.clear();
		m_samplerCubeViews.clear();
		m_samplerCubeViewHashToID.clear();
	}

private:
	std::mutex m_combinedMutex, m_samplerCubeMutex;
	std::vector<VkDescriptorImageInfo> m_combinedViews, m_samplerCubeViews;
	std::unordered_map<ImageViewSamplerKey, uint32_t, HashPair, EqualPair> m_combinedViewHashToID, m_samplerCubeViewHashToID;
};

struct PassTimestampRange
{
	uint32_t beginQuery = UINT32_MAX;
	uint32_t endQuery = UINT32_MAX;
};

// Indirect draw buffer can fit in lots of different draws
struct IndirectDrawRange
{
	uint32_t firstCommand = 0;   // first indirect command index in frameCtx.indirectDraws
	uint32_t commandCount = 0;   // number of VkDrawIndexedIndirectCommand entries for this pass
	uint32_t visibleCount = 0;   // number of visible instances (sum of cmd.instanceCount)
	uint32_t firstInstance = 0;
};


struct alignas(16) SpecularPrefilterPush
{
	float roughness;
	uint32_t width;
	uint32_t height;
	uint32_t sampleCount;
};

//struct EnvironmentSet
//{
//	AllocatedImage irradiance;
//	AllocatedImage specular;
//	AllocatedImage skybox;
//	uint32_t setIndex = UINT32_MAX;
//	std::vector<SpecularPrefilterPush> specularPCs{}; 
//	AllocatedImage equirect; // Temp image
//};
//
//struct TotalAssetDataCounts
//{
//	uint32_t totalVertexCount = 0u;
//	uint32_t totalIndexCount = 0u;
//	uint32_t totalMaterialCount = 0u;
//	uint32_t totalMeshCount = 0u;
//	uint32_t totalTransformCount = 0u;
//};
//
//
//struct RuntimeImage {
//	AllocatedImage image;
//	MaterialType semantic{ MaterialType::Unknown };
//};

//struct MaterialResources {
//	AllocatedImage albedoImage;
//	VkSampler albedoSampler;
//
//	AllocatedImage metalRoughImage;
//	VkSampler metalRoughSampler;
//
//	AllocatedImage normalImage;
//	VkSampler normalSampler;
//
//	AllocatedImage emissiveImage;
//	VkSampler emissiveSampler;
//};


struct DirtyRange {
	uint32_t offset = 0;
	uint32_t count = 0;
};

// === Per-frame sync ===
// Compares current VirtualInstance.usedCopies/firstTransform to visState.slabs and decides:
//  - grow: appendSceneCopies + buildBVH()
//  - shrink: shrinkSceneCopiesLazy + buildBVH()
//  - relocate-only: rewriteSceneSlice + refitBVH()
//  - no change: do nothing
// Returns whether topology changed or a refit-only is needed, plus any transform upload ranges.
struct VisibilitySyncResult
{
	bool topologyChanged = false;   // grew/shrank -> call buildBVH()
	bool refitOnly = false;         // only transforms changed -> call refitBVH()
	std::vector<DirtyRange> dirtyTransformRanges; // for GPU uploads
};

// Virtual control over instances, enables true instancing with unique transforms
struct VirtualInstance
{
	uint32_t instanceID = UINT32_MAX;         // The singular model index tag
	uint8_t sceneID = UINT8_MAX;              // Unordered map id to map this back to loadedScenes
	RD::InstancingMethod drawType =
		RD::InstancingMethod::DrawStatic;     // Controls how an asset is treated in drawing
	glm::vec3 modelOffset{ 0.0f };            // Divides spacing in world space between models

	// Only first transform should change at runtime to move through the global transform vector
	glm::mat4 baseTransform = glm::mat4(0.0f); // First transform matrix in global list
	uint32_t firstTransform = 0;               // start of this instance's transform slab (copy 0, slot 0)
	uint32_t transformCount = 0;               // transforms PER COPY (unique node slots)
	uint32_t perInstanceStride = 0;            // rows PER COPY (meshes/primitives in bakedInstances)

	uint32_t usedCopies = 1;                   // active copies (drawn / in VisibilityState)
	uint32_t capacityCopies = 1;               // allocated copies in transform slab (contiguous storage)

	float spinAngleRadians = 0.0f;
	float movePhaseRadians = 0.0f;
};



// For use in bend studios screen space contact shadows
struct DispatchData {
	glm::ivec3 waveCount{0};   // Dispatch values passed to compute scope
	glm::ivec2 waveOffset{0};  // Offset passed to push constant equivalent
};
// For use in bend studios screen space contact shadows
struct DispatchList {
	glm::vec4 lightCoords{0.0f}; // Same between all disptaches

	DispatchData dispatch[8]{};
	int dispatchCount = 0;
};

// === RENDER PASS PUSH CONSTANTS ===

struct alignas(16) SSAOPush {
	glm::vec2 tanHalfFov{0.0f};
	float effectRadius = 0.5f;
	float effectFalloffRange = 0.6f;

	glm::vec2 ndcToViewAdd{ 0.0f };
	glm::vec2 ndcToViewMul{ 0.0f };

	glm::vec2 ndcToViewMul_x_PixelSize{ 0.0f };

	float depthLinearizeMult = 0.0f;
	float depthLinearizeAdd = 0.0f;

	// Bilateral blur
	float sharpness = 2.0;
	float radius = 4.0;
	glm::vec2 blurDirection{ 0.0f };

	// For temporal noise
	uint32_t noiseIndex = 0u; // FrameIndex % 64u
	uint32_t hilbertLutID = UINT32_MAX;

	// Denoise
	float denoiseBlurBeta = 1.2f;
	uint32_t isFinalPass = 0u;
};

struct alignas(16) TAAPush {
	float minBlend = 0.05f;
	float maxBlend = 0.5f;
	float depthDisocclusionScale = 200.0f;
	float pad0;
};

struct alignas(16) VolumetricPush {
	float density = 0.002f;
	float scatteringStrength = 15.0f;
	float extinction = 0.08f;
	float heightFalloff = 0.05f;

	float maxDistance = 200.0f;
	float jitterStrength = 0.8f;
	float asymmetryFactor = 0.9f;
	float minTransmittance = 0.9f;

	int beamPower = 2;
	float blurRadius = 4.0f;
	float blurDepthSigma = 0.015f;
	float blurWeightSigma = 1.6f;

	glm::vec2 blurDirection{ 0.0f };
	glm::vec2 pad0{ 0.0f };
};

struct alignas(16) LensFlarePush {
	// Quarter res
	glm::vec2 outputRes{ 0.0f };
	glm::vec2 invOutputRes{ 0.0f };

	glm::vec2 sunUv{ 0.5f, 0.5f };
	float sunVisible = 1.0f;
	uint32_t rainbowLUTIndex = 0u;

	// Bright-pass params (FlareBright)
	float brightThreshold = 15.0f;
	float brightKnee = 7.5f; // 0.5 * threshold
	float brightIntensity = 1.0f;
	float pad0{ 0.0f };

	// Ring params (FlareGen)
	float ringInnerRadius = 0.1f;
	float ringOuterRadius = 0.20f;
	float chromaStrength = 1.0f;
	float pad1{ 0.0f };

	// Streak params (FlareGen)
	float streakStrength = 0.2f;
	float streakWidth = 0.01f;   // UV units
	float streakLength = 0.2f;   // UV units
	float pad2{ 0.0f };

	// Hi-Z occlusion params (FlareGen)
	float occlusionRadiusPixels = 0.01f;  // full-res pixels
	float occlusionDepthBias = 0.01f;     // linear depth bias
	float occlusionFade = 8.0f;           // higher = harder fade
	float pad3{ 0.0f };
};

// Active IDs map 1:1 with worldaabbs
struct alignas(16) VisibilityPush {
	uint64_t activeIDsBufferAddr;
	uint64_t worldAABBsBufferAddr;
	uint32_t activeCount;
	uint32_t hizEnabled;
	uint32_t pad0;
	uint32_t pad1;
};

// Screen space contact shadows usage
struct alignas(16) SSSPush {
	glm::vec4 lightCoords{0.0f};

	glm::ivec2 waveOffsets{0};
	glm::vec2 invDepthSize{0.0f};

	float surfaceThickness = 0.01f;
	float bilinearThreshold = 0.02f;
	float shadowContrast = 4.0f;
	float pad0;
};

struct alignas(16) ForwardPush {
	uint32_t activeLightCount;
	float oitDepthScale = 400.0f;
	uint32_t flashlightShadowMapID = UINT32_MAX;
	uint32_t flashlightCookieTexID = UINT32_MAX;
	glm::mat4 flashlightVP;
};

struct alignas(16) CMAA2Push {
	uint32_t halfWidth;
	uint32_t maxShapeCandidates;
	uint32_t maxDeferredItems;
	uint32_t maxDeferredLocations;
	// x: symmetry correction offset, y: dampening effect, z: simple blurriness
	glm::vec4 params = glm::vec4(0.22f, 0.15f, 0.1f, 0.0f);
};

struct alignas(16) BindlessAccessPush {
	uint32_t id0 = UINT32_MAX;
	uint32_t id1 = UINT32_MAX;
	uint32_t id2 = UINT32_MAX;
	uint32_t id3 = UINT32_MAX;
};

struct ToneMappingSettings {
	float cameraExposure = 0.18f;
	float maxLuminance = 0.0f;
	float midLuminance = 0.0f;
	float minLuminance = 0.0f;
};

// An instance basically = mesh
struct Instance
{
	uint32_t instanceID   = UINT32_MAX;  // Unique runtime tag for a renderables list *not current used
	uint32_t meshID       = UINT32_MAX;  // global meshBuffer
	uint32_t materialID   = UINT32_MAX;  // global material buffer
	uint32_t transformID  = UINT32_MAX;  // global transform/prevTransform buffer
	uint32_t passType     = UINT32_MAX;  // opaque/transparent material pass
};



// UNIFORM BUFFER TYPES
struct alignas(16) SceneInfo
{
	glm::mat4 view{};
	glm::mat4 proj{};
	glm::mat4 invView{};
	glm::mat4 invProj{};
	glm::mat4 viewProj{};
	glm::mat4 prevViewProj{};
	glm::mat4 prevView{};
	glm::mat4 viewProjUnjittered{};
	// x = frameIndex, y = historyValid (0/1), z = Hi-Z valid(0/1)
	glm::uvec4 temporal{};
	// x = current jitter x ndc
	// y = current jitter y
	// z = previous jitter x
	// w = previous jitter y
	glm::vec4 temporalJitter{};
	// w for sun power
	glm::vec4 sunlightDirection{};
	glm::vec4 sunlightColor{};
	glm::vec4 cameraPos{};         // xyz pos, .w exposure
	glm::vec4 cameraClips{};       // .x near and .y far
	glm::vec4 viewportSize{};      // .x and .y for width and height, .z for pixel count
	glm::vec4 pixelSizes{};        // .x/.y = 1 / full extent .z/.w = = 1 / half extent
};

// x = diffuse, y = specular, z = brdf, w = skybox
struct alignas(16) EnvironmentIndexArray {
	glm::uvec4 indices[RD::MAX_ENV_SETS];
};

struct alignas(16) DirectionalCSMInfo
{
	glm::mat4 cascadeVP[RD::MAX_SHADOW_CASCADES]{0.0f};
	glm::vec4 cascadeSplits{0.0f};
	// x=bias, y=shadowAtlasID, z=cascadeCount, w=atlasTexelSize
	glm::vec4 params{ 0.0f };
	// xy = uvScale, zw = uvOffset (per cascade)
	glm::vec4 atlasUV[RD::MAX_SHADOW_CASCADES]{};
	glm::vec4 maxFilterRadiusTexels{};
	float cascadeBias[RD::MAX_SHADOW_CASCADES]{};
	//float cascadeNormalOffset[MAX_SHADOW_CASCADES]{};
};
