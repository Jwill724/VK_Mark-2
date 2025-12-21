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
	uint32_t storageImageIndex = UINT32_MAX;
	uint32_t samplerCubeIndex = UINT32_MAX;

	// Used for single index and non lut entry structs
	static constexpr ImageLUTEntry CombinedOnly(uint32_t id) {
		return ImageLUTEntry{ .combinedImageIndex = id };
	}
	static constexpr ImageLUTEntry StorageOnly(uint32_t id) {
		return ImageLUTEntry{ .storageImageIndex = id };
	}
	static constexpr ImageLUTEntry SamplerCubeOnly(uint32_t id) {
		return ImageLUTEntry{ .samplerCubeIndex = id };
	}

	inline void reset() noexcept {
		combinedImageIndex = UINT32_MAX;
		storageImageIndex = UINT32_MAX;
		samplerCubeIndex = UINT32_MAX;
	}
};

struct ImageLUTManager {
	std::vector<ImageLUTEntry> entries;
	std::unordered_set<uint32_t> pushedCombined;
	std::unordered_set<uint32_t> pushedStorage;
	std::unordered_set<uint32_t> pushedCube;
	std::mutex mutex;

	void addEntry(const ImageLUTEntry& entry) {
		std::scoped_lock lock(mutex);

		bool alreadyAdded = false;

		if (entry.combinedImageIndex != UINT32_MAX &&
			pushedCombined.insert(entry.combinedImageIndex).second) {
			alreadyAdded = true;
		}
		if (entry.storageImageIndex != UINT32_MAX &&
			pushedStorage.insert(entry.storageImageIndex).second) {
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
		pushedStorage.clear();
		pushedCube.clear();
	}

	~ImageLUTManager() {
		clear();
	}

	const std::vector<ImageLUTEntry>& getEntries() const { return entries; }
};

struct ImageTable {
	std::mutex combinedMutex, storageMutex, samplerCubeMutex;

	std::vector<VkDescriptorImageInfo> combinedViews;
	std::vector<VkDescriptorImageInfo> storageViews;
	std::vector<VkDescriptorImageInfo> samplerCubeViews;

	std::unordered_map<ImageViewSamplerKey, uint32_t, HashPair, EqualPair> combinedViewHashToID;
	std::unordered_map<ImageViewSamplerKey, uint32_t, HashPair, EqualPair> samplerCubeViewHashToID;
	std::unordered_map<size_t, uint32_t> storageViewHashToID;

	void clearTables() {
		std::scoped_lock l1(combinedMutex, storageMutex, samplerCubeMutex);
		combinedViews.clear();
		combinedViewHashToID.clear();
		storageViews.clear();
		storageViewHashToID.clear();
		samplerCubeViews.clear();
		samplerCubeViewHashToID.clear();
	}

	static ImageViewSamplerKey makeKey(VkImageView view, VkSampler sampler) {
		return { view, sampler };
	}

	uint32_t pushCombined(VkImageView view, VkSampler sampler, ThreadContext* threadCtx = nullptr);
	uint32_t pushStorage(VkImageView view, ThreadContext* threadCtx = nullptr);
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

inline uint32_t ImageTable::pushStorage(VkImageView view, ThreadContext* threadCtx) {
	std::scoped_lock lock(storageMutex);
	ASSERT(view && "Null handle in pushStorage");

	size_t hash = std::hash<std::uintptr_t>{}(reinterpret_cast<std::uintptr_t>(view));
	if (auto it = storageViewHashToID.find(hash); it != storageViewHashToID.end())
		return it->second;

	VkDescriptorImageInfo info { VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL };
	uint32_t index = static_cast<uint32_t>(storageViews.size());
	storageViews.push_back(info);
	storageViewHashToID[hash] = index;

	if (ENABLE_DEBUG_LOGS) {
		if (threadCtx == nullptr) {
			fmt::print("[ImageTable::pushStorage] New [{}] = view={}\n", index, (void*)view);
		}
		else {
			JobSystem::log(threadCtx->threadID,
				fmt::format("[ImageTable::pushStorage] New [{}] = view={}\n", index, (void*)view));
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
	uint32_t addStorageImage(VkImageView view, ThreadContext* threadCtx = nullptr) {
		return table.pushStorage(view, threadCtx);
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

	VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkImageLayout finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

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
	bool enableDepthWrite = true;
	VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

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

//struct alignas(16) CullingPushConstantsAddrs {
//	glm::vec4 frusPlanes[6];
//	uint64_t meshIDBufferAddr;
//	uint64_t visibleMeshOutBufferAddr;
//	glm::vec4 frusPoints[8];
//	uint64_t visibleCountOutBufferAddr;
//	uint32_t meshCount;
//	uint32_t rebuildTransforms;
//};
//static_assert(sizeof(CullingPushConstantsAddrs) == 256);

// Opaque and transparent distinction in shared instance/indirect cmd buffers
struct PassRange {
	uint32_t first = 0;
	uint32_t visibleCount = 0;
};

// TODO: Make this range shit clearer
// world aabb rows within VisibilityState
struct DirtyRange { uint32_t offset; uint32_t count; };

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
	uint32_t instanceID = UINT32_MAX; // flat list
	uint8_t sceneID = UINT8_MAX;      // unordered map id
	DrawType drawType = DrawType::DrawStatic;
	glm::vec3 modelOffset{ 0.0f };

	uint32_t firstTransform = 0;    // slab start in the global list
	uint32_t transformCount = 0;    // unique transforms
	uint32_t perInstanceStride = 0; // rows/primitives
	uint32_t usedCopies = 1;        // realized copies in this slab
	uint32_t capacityCopies = 1;    // reserved copies in this slab
};

// In mesh setup all model vertices/indices are collected
// to be batched in one upload
struct UploadMeshContext {
	std::vector<uint32_t> globalIndices;
	std::vector<Vertex> globalVertices;
};

struct MeshRegistry {
	std::vector<GPUMeshData> meshData;

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
	VkClearValue clearValue{};
};

// === RENDER PASS PUSH CONSTANTS ===

// Ambient occlusion method push constants
struct alignas(16) SSAOPush {
	glm::vec2 screenSize{ 0.0f };
	glm::vec2 invScreenSize{ 0.0f };
	float aoRadius = 0.5f;
	float bias = 0.01f;
	float intensity = 1.5f;
	int blurRadius = 5;
	glm::vec2 blurDirection;
	uint32_t sampleCount = 64;
	float pad0{ 0.0f };
};
struct alignas(16) GTAOPush {
	glm::vec2 ndcToViewMul{ 0.0f };
	glm::vec2 tanHalfFov{0.0f};

	glm::vec2 ndcToViewAdd{ 0.0f };
	glm::vec2 pixelSize{ 0.0f };

	float effectRadius = 0.2f;
	float radiusMultiplier = 1.457f;
	float effectFalloffRange = 0.7f;
	float sampleDistributionPower = 2.5f;

	float thinOccluderCompensation = 0.5f;
	float depthMipSamplingOffset = 3.3f;
	glm::vec2 ndcToViewMul_x_PixelSize{ 0.0f };

	uint32_t sliceCount = 8;
	uint32_t stepsPerSliceCount = 4;
	// Alongside pixelSize, needed during filtering
	float sharpness = 2.0;
	float radius = 4.0;

	glm::vec2 blurDirection{ 0.0f };
	glm::vec2 pad0{ 0.0f };
};

struct alignas(16) VolumetricPush {
	float density = 0.05f;
	float scatteringStrength = 5.0f;
	float extinction = 0.08f;
	float heightFalloff = 0.04f;

	float maxDistance = 100.0f;
	float jitterStrength = 0.8f;
	int stepCount = 32;
	float pad0{ 0.0f };

	float asymmetryFactor = 0.9f;
	float minTransmittance = 0.9f;
	glm::vec2 pixelSize{ 0.0f };

	int beamPower = 2;
	float blurRadius = 2.5f;
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
	float ringInnerRadius = 0.11f;
	float ringOuterRadius = 0.17f;
	float chromaStrength = 1.0f;
	float pad1{ 0.0f };

	// Streak params (FlareGen)
	float streakStrength = 0.1f;
	float streakWidth = 0.001f; // UV units
	float streakLength = 0.05f; // UV units
	float pad2{ 0.0f };

	// Hi-Z occlusion params (FlareGen)
	float occlusionRadiusPixels = 0.01f;  // full-res pixels
	float occlusionDepthBias = 0.01f;     // linear depth bias
	float occlusionFade = 8.0f;           // higher = harder fade
	float pad3{ 0.0f };
};