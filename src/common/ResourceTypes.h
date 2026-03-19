#pragma once

#include "Vk_Types.h"
#include "common/ErrorChecking.h"
#include "engine/JobSystem.h"

using ImageViewSamplerKey = std::pair<VkImageView, VkSampler>;

struct HashPair {
	size_t operator()(const ImageViewSamplerKey& key) const {
		return std::hash<std::uintptr_t>()(reinterpret_cast<std::uintptr_t>(key.first)) ^
			(std::hash<std::uintptr_t>()(reinterpret_cast<std::uintptr_t>(key.second)) << 1);
	}
};

struct EqualPair {
	bool operator()(const ImageViewSamplerKey& a, const ImageViewSamplerKey& b) const {
		return a.first == b.first && a.second == b.second;
	}
};

struct ImageLUTEntry {
	uint32_t combinedImageIndex = UINT32_MAX;
	uint32_t samplerCubeIndex = UINT32_MAX;

	// Used for single index and non lut entry structs
	static constexpr ImageLUTEntry CombinedOnly(uint32_t id) {
		return ImageLUTEntry{ .combinedImageIndex = id };
	}
	static constexpr ImageLUTEntry SamplerCubeOnly(uint32_t id) {
		return ImageLUTEntry{ .samplerCubeIndex = id };
	}

	inline void reset() noexcept {
		combinedImageIndex = UINT32_MAX;
		samplerCubeIndex = UINT32_MAX;
	}
};

struct ImageLUTManager {
	std::vector<ImageLUTEntry> entries;
	std::unordered_set<uint32_t> pushedCombined;
	std::unordered_set<uint32_t> pushedCube;
	std::mutex mutex;

	void addEntry(const ImageLUTEntry& entry) {
		std::scoped_lock lock(mutex);

		bool alreadyAdded = false;

		if (entry.combinedImageIndex != UINT32_MAX &&
			pushedCombined.insert(entry.combinedImageIndex).second) {
			alreadyAdded = true;
		}
		if (entry.samplerCubeIndex != UINT32_MAX &&
			pushedCube.insert(entry.samplerCubeIndex).second) {
			alreadyAdded = true;
		}
		if (alreadyAdded) {
			entries.emplace_back(entry);
		}
	}

	void clear() {
		std::scoped_lock lock(mutex);
		entries.clear();
		pushedCombined.clear();
		pushedCube.clear();
	}

	~ImageLUTManager() {
		clear();
	}

	const std::vector<ImageLUTEntry>& getEntries() const { return entries; }
};

struct ImageTable {
	std::mutex combinedMutex, samplerCubeMutex;

	std::vector<VkDescriptorImageInfo> combinedViews;
	std::vector<VkDescriptorImageInfo> samplerCubeViews;

	std::unordered_map<ImageViewSamplerKey, uint32_t, HashPair, EqualPair> combinedViewHashToID;
	std::unordered_map<ImageViewSamplerKey, uint32_t, HashPair, EqualPair> samplerCubeViewHashToID;

	void clearTables() {
		std::scoped_lock l1(combinedMutex, samplerCubeMutex);
		combinedViews.clear();
		combinedViewHashToID.clear();
		samplerCubeViews.clear();
		samplerCubeViewHashToID.clear();
	}

	static ImageViewSamplerKey makeKey(VkImageView view, VkSampler sampler) {
		return { view, sampler };
	}

	uint32_t pushCombined(VkImageView view, VkSampler sampler, ThreadContext* threadCtx = nullptr);
	uint32_t pushSamplerCube(VkImageView view, VkSampler sampler, ThreadContext* threadCtx = nullptr);
};

inline uint32_t ImageTable::pushCombined(VkImageView view, VkSampler sampler, ThreadContext* threadCtx) {
	std::scoped_lock lock(combinedMutex);
	ASSERT(view && sampler && "Null handle in pushCombined");
	auto key = makeKey(view, sampler);

	if (auto it = combinedViewHashToID.find(key); it != combinedViewHashToID.end())
		return it->second;

	VkDescriptorImageInfo info { sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	uint32_t index = static_cast<uint32_t>(combinedViews.size());
	combinedViews.push_back(info);
	combinedViewHashToID[key] = index;

	if (ENABLE_DEBUG_LOGS) {
		if (threadCtx == nullptr) {
			fmt::print("[ImageTable::pushCombined] New [{}] = view={}, sampler={}\n", index, (void*)view, (void*)sampler);
		}
		else {
			JobSystem::log(threadCtx->threadID,
				fmt::format("[ImageTable::pushCombined] New [{}] = view={}, sampler={}\n", index, (void*)view, (void*)sampler));
		}
	}

	return index;
}

inline uint32_t ImageTable::pushSamplerCube(VkImageView view, VkSampler sampler, ThreadContext* threadCtx) {
	std::scoped_lock lock(samplerCubeMutex);
	ASSERT(view && sampler && "Null handle in pushSamplerCube");
	auto key = makeKey(view, sampler);

	if (auto it = samplerCubeViewHashToID.find(key); it != samplerCubeViewHashToID.end())
		return it->second;

	VkDescriptorImageInfo info { sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	uint32_t index = static_cast<uint32_t>(samplerCubeViews.size());
	samplerCubeViews.push_back(info);
	samplerCubeViewHashToID[key] = index;

	if (ENABLE_DEBUG_LOGS) {
		if (threadCtx == nullptr) {
			fmt::print("[ImageTable::pushSamplerCube] New [{}] = view={}, sampler={}\n", index, (void*)view, (void*)sampler);
		}
		else {
			JobSystem::log(threadCtx->threadID,
				fmt::format("[ImageTable::pushSamplerCube] New [{}] = view={}, sampler={}\n", index, (void*)view, (void*)sampler));
		}
	}
	return index;
}

// Controls bindless image creation, storing indexes
struct ImageTableManager {
	ImageTable table;

	uint32_t addCombinedImage(VkImageView view, VkSampler sampler, ThreadContext* threadCtx = nullptr) {
		return table.pushCombined(view, sampler, threadCtx);
	}
	uint32_t addCubeImage(VkImageView view, VkSampler sampler, ThreadContext* threadCtx = nullptr) {
		return table.pushSamplerCube(view, sampler, threadCtx);
	}

	void clear() {
		table.clearTables();
	}

	~ImageTableManager() {
		clear();
	}
};

struct AllocatedImage {
	VkImage image = VK_NULL_HANDLE;
	VkImageView imageView = VK_NULL_HANDLE;
	std::vector<VkImageView> storageViews{};
	bool perMipStorageViews = false;
	VkFormat format = VK_FORMAT_UNDEFINED;
	VkExtent3D extent{};
	uint32_t mipLevelCount = 0;
	uint32_t arrayLayers = 1;
	std::vector<VkImageView> layerViews{}; // Visual debugging a 2d array

	VkImageType imageType = VK_IMAGE_TYPE_2D;
	VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;

	VkImageLayout previousLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocation allocation = nullptr;
	ImageLUTEntry lutEntry;

	bool mipmapped = false;
	bool isCubeMap = false;
};

struct alignas(16) SpecularPC {
	float roughness;
	uint32_t width;
	uint32_t height;
	uint32_t sampleCount;
};

struct EnvironmentSet {
	AllocatedImage irradiance;
	AllocatedImage specular;
	AllocatedImage skybox;
	uint32_t setIndex = UINT32_MAX;
	std::vector<SpecularPC> specularPCs{};
	AllocatedImage equirect; // Temp image
};

struct ModelDataCounts {
	uint32_t totalVertexCount = 0u;
	uint32_t totalIndexCount = 0u;
	uint32_t totalMaterialCount = 0u;
	uint32_t totalMeshCount = 0u;
	uint32_t totalTransformCount = 0u;
};


// Defines push constants usages
struct PushConstantDef {
	uint32_t offset;
	uint32_t size;
	VkShaderStageFlags stageFlags;
};

// Holds pipeline layout and push constant data
// All pipelines use the same setup so its globally accessible
struct PipelineLayoutConst {
	VkPipelineLayout layout;
	PushConstantDef pcRange;
};

struct ShaderStageInfo {
	VkShaderStageFlagBits stage;
	const char* filePath;
};

struct DescriptorInfo {
	VkDescriptorType type;
	uint32_t binding = UINT32_MAX;
	VkShaderStageFlags stageFlags;

	void* pNext = nullptr;
};

enum class PipelineCategory {
	Raster,  // Vertex/frag traditional
	Compute, // Comptue shader
	Count
};

struct PipelineHandle {
	VkPipeline pipeline = VK_NULL_HANDLE;
	PipelineCategory type = PipelineCategory::Count;
	std::string name;
	bool swappable = false;
	VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_MAX_ENUM;
	VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
};

struct PipelinePreset {
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

	// Default pipeline settings
	VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
	VkCullModeFlagBits cullMode = VK_CULL_MODE_NONE;
	VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	bool enableBlending = false;
	bool enableDepthTest = true;
	bool enableDepthWrite = false;
	VkCompareOp depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;

	bool enableDepthBias = false;
	float depthBiasConstant = 0.0f;
	float depthBiasSlope = 0.0f;
	float depthBiasClamp = 0.0f;

	uint32_t viewMask = 0;

	std::vector<VkFormat>colorFormats;
	VkFormat depthFormat = VK_FORMAT_UNDEFINED;

	std::vector<ShaderStageInfo> shaderStagesInfo;
};

struct AllocatedBuffer {
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceAddress address = UINT64_MAX;
	VmaAllocation allocation{};
	VmaAllocationInfo info{};
	void* mapped = nullptr;

	// buffer sharing
	bool isConcurrent = false;
	uint8_t qmask = 0; // bit0=graphics, bit1=transfer, bit2=compute
};

enum class PassID : uint16_t {
	None = 0,

	Prepass,
	HiZGeneration,
	ClusteredLightBuild,
	GTAO,
	DirectionalCSM,
	FlashlightShadow,
	ScreenSpaceContactShadows,
	Skybox,
	OpaqueForward,
	OBBLineView,
	TransparentForward,
	VolumetricLighting,
	TAA,
	Exposure,
	LensFlare,
	FinalComposite,
	CMAA2,
	SMAA,
	FXAA,
	ChromaticAberration,

	Count
};

enum class TextureSemantic : uint8_t {
	Unknown = 0,
	BaseColor,
	Normal,
	MetalRoughness,
	Occlusion,
	Emissive
};

struct RuntimeImage {
	AllocatedImage image;
	TextureSemantic semantic{ TextureSemantic::Unknown };
};

// Indirect draw buffer can fit in lots of different draws
struct PassRange {
	uint32_t firstCommand = 0;   // first indirect command index in frameCtx.indirectDraws
	uint32_t commandCount = 0;   // number of VkDrawIndexedIndirectCommand entries for this pass
	uint32_t visibleCount = 0;   // number of visible instances (sum of cmd.instanceCount)
	uint32_t firstInstance = 0;
};


// TODO: Make this range shit clearer
// world aabb rows within VisibilityState
struct DirtyRange {
	uint32_t offset = 0;
	uint32_t count = 0;
};

// === Per-frame sync ===
// Compares current GlobalInstance.usedCopies/firstTransform to visState.slabs and decides:
//  - grow: appendSceneCopies + buildBVH()
//  - shrink: shrinkSceneCopiesLazy + buildBVH()
//  - relocate-only: rewriteSceneSlice + refitBVH()
//  - no change: do nothing
// Returns whether topology changed or a refit-only is needed, plus any transform upload ranges.
struct VisibilitySyncResult {
	bool topologyChanged = false;   // grew/shrank -> call buildBVH()
	bool refitOnly = false;         // only transforms changed -> call refitBVH()
	std::vector<DirtyRange> dirtyTransformRanges; // for GPU uploads
};

// Virtual control over instances, enables true instancing with unique transforms
struct GlobalInstance {
	uint32_t instanceID = UINT32_MAX;         // The singular model index tag
	uint8_t sceneID = UINT8_MAX;              // Unordered map id to map this back to loadedScenes
	DrawType drawType = DrawType::DrawStatic; // Controls how an asset is treated in drawing
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

// In mesh setup all model vertices/indices are collected
// to be batched in one upload
struct UploadMeshContext {
	std::vector<uint32_t> globalIndices;
	std::vector<Vertex> globalVertices;
};

struct MeshLODs {
	uint32_t lod0 = UINT32_MAX;
	uint32_t lod1 = UINT32_MAX;
	uint32_t lod2 = UINT32_MAX;
	uint32_t lod3 = UINT32_MAX;

	uint32_t shadowLod0 = UINT32_MAX;
	uint32_t shadowLod1 = UINT32_MAX;
	uint32_t shadowLod2 = UINT32_MAX;

	uint32_t flags = 0;
};
constexpr uint32_t MESH_LOD_FLAG_FORCE_SHADOW_LOD0 = 1u << 0;

struct MeshRegistry {
	std::vector<GPUMeshData> meshData;
	std::vector<MeshLODs> meshLODs;

	// holds a linear list of meshIDs for gpu access
	AllocatedBuffer meshIDBuffer;

	inline std::vector<uint32_t> extractAllMeshIDs() const {
		std::vector<uint32_t> ids;
		ids.reserve(meshData.size());

		for (uint32_t id = 0; id < meshData.size(); ++id) {
			ids.push_back(id);
		}

		return ids;
	}

	inline uint32_t registerMesh(const GPUMeshData& data) {
		uint32_t id = static_cast<uint32_t>(meshData.size());
		ASSERT(id != std::numeric_limits<uint32_t>::max() && "MeshRegistry: MeshID overflow!");

		meshData.push_back(data);

		MeshLODs lods{};
		lods.lod0 = id;
		lods.lod1 = id;
		lods.lod2 = id;
		lods.lod3 = id;
		meshLODs.push_back(lods);

		return id;
	}
};


struct MaterialResources {
	AllocatedImage albedoImage;
	VkSampler albedoSampler;

	AllocatedImage metalRoughImage;
	VkSampler metalRoughSampler;

	AllocatedImage aoImage;
	VkSampler aoSampler;

	AllocatedImage normalImage;
	VkSampler normalSampler;

	AllocatedImage emissiveImage;
	VkSampler emissiveSampler;
};

struct AttachmentDesc {
	VkImageView imageView = VK_NULL_HANDLE;
	VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkResolveModeFlagBits resolveMode = VK_RESOLVE_MODE_NONE;
	VkImageView resolveView = VK_NULL_HANDLE;
	VkImageLayout resolveLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	VkClearValue clearValue{ 0.0f };
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

struct alignas(16) GTAOPush {
	glm::vec2 ndcToViewMul{ 0.0f };
	glm::vec2 tanHalfFov{0.0f};

	glm::vec2 ndcToViewAdd{ 0.0f };
	// 4x4 looks better and runs faster than ssao.
	uint32_t sliceCount = 4;
	uint32_t stepsPerSliceCount = 5;

	float effectRadius = 0.4f;
	float radiusMultiplier = 1.457f;
	float effectFalloffRange = 0.6f;
	float sampleDistributionPower = 3.0f;

	float thinOccluderCompensation = 0.9f;
	float depthMipSamplingOffset = 3.3f;
	glm::vec2 ndcToViewMul_x_PixelSize{ 0.0f };

	// Alongside pixelSize, needed during filtering
	float sharpness = 2.0;
	float radius = 4.0;
	glm::vec2 blurDirection{ 0.0f };
};

struct alignas(16) TAAPush {
	float minBlend = 0.05f;
	float maxBlend = 0.5f;
	float depthDisocclusionScale = 200.0f;
	float pad0;
};

struct alignas(16) VolumetricPush {
	float density = 0.005f;
	float scatteringStrength = 30.0f;
	float extinction = 0.08f;
	float heightFalloff = 0.05f;

	float maxDistance = 200.0f;
	float jitterStrength = 0.8f;
	int stepCount = 48;
	float pad0;

	float asymmetryFactor = 0.9f;
	float minTransmittance = 0.9f;
	glm::vec2 pixelSize{ 0.0f };

	int beamPower = 2;
	float blurRadius = 2.0f;
	float blurDepthSigma = 1.5f;
	float blurWeightSigma = 5.0f;

	glm::vec2 blurDirection{ 0.0f };
	glm::vec2 pad1{ 0.0f };
};

struct alignas(16) LensFlarePush {
	glm::vec2 fullRes{ 0.0f };
	glm::vec2 invFullRes{ 0.0f };

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
	float ringInnerRadius = 0.05f;
	float ringOuterRadius = 0.20f;
	float chromaStrength = 1.0f;
	float pad1{ 0.0f };

	// Streak params (FlareGen)
	float streakStrength = 0.1f;
	float streakWidth = 0.01f;   // UV units
	float streakLength = 0.1f;   // UV units
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
	float pad0;
	float pad1;
	float pad2;
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

struct PassTimestampRange {
	uint32_t beginQuery = UINT32_MAX;
	uint32_t endQuery = UINT32_MAX;
};
