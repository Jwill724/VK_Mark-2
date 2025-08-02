#pragma once

#include "Vk_Types.h"
#include "ErrorChecking.h"

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

//enum class ImageLUTType {
//	Global,
//	MaterialTextures
//};

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

	uint32_t pushCombined(VkImageView view, VkSampler sampler);
	uint32_t pushStorage(VkImageView view);
	uint32_t pushSamplerCube(VkImageView view, VkSampler sampler);
};

inline uint32_t ImageTable::pushCombined(VkImageView view, VkSampler sampler) {
	std::scoped_lock lock(combinedMutex);
	ASSERT(view && sampler && "Null handle in pushCombined");
	auto key = makeKey(view, sampler);

	if (auto it = combinedViewHashToID.find(key); it != combinedViewHashToID.end())
		return it->second;

	VkDescriptorImageInfo info { sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	uint32_t index = static_cast<uint32_t>(combinedViews.size());
	combinedViews.push_back(info);
	combinedViewHashToID[key] = index;

	fmt::print("[ImageTable::pushCombined] New [{}] = view={}, sampler={}\n", index, (void*)view, (void*)sampler);
	return index;
}

inline uint32_t ImageTable::pushSamplerCube(VkImageView view, VkSampler sampler) {
	std::scoped_lock lock(samplerCubeMutex);
	ASSERT(view && sampler && "Null handle in pushSamplerCube");
	auto key = makeKey(view, sampler);

	if (auto it = samplerCubeViewHashToID.find(key); it != samplerCubeViewHashToID.end())
		return it->second;

	VkDescriptorImageInfo info { sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	uint32_t index = static_cast<uint32_t>(samplerCubeViews.size());
	samplerCubeViews.push_back(info);
	samplerCubeViewHashToID[key] = index;

	fmt::print("[ImageTable::pushSamplerCube] New [{}] = view={}, sampler={}\n", index, (void*)view, (void*)sampler);
	return index;
}

inline uint32_t ImageTable::pushStorage(VkImageView view) {
	std::scoped_lock lock(storageMutex);
	ASSERT(view && "Null handle in pushStorage");

	size_t hash = std::hash<std::uintptr_t>{}(reinterpret_cast<std::uintptr_t>(view));
	if (auto it = storageViewHashToID.find(hash); it != storageViewHashToID.end())
		return it->second;

	VkDescriptorImageInfo info { VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL };
	uint32_t index = static_cast<uint32_t>(storageViews.size());
	storageViews.push_back(info);
	storageViewHashToID[hash] = index;

	fmt::print("[ImageTable::pushStorage] New [{}] = view={}\n", index, (void*)view);
	return index;
}

struct ImageTableManager {
	ImageTable table;

	uint32_t addCombinedImage(VkImageView view, VkSampler sampler) {
		return table.pushCombined(view, sampler);
	}
	uint32_t addStorageImage(VkImageView view) {
		return table.pushStorage(view);
	}
	uint32_t addCubeImage(VkImageView view, VkSampler sampler) {
		return table.pushSamplerCube(view, sampler);
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
	// Only real distinction between imageview and storageview is imagetype
	VkImageView imageView = VK_NULL_HANDLE; // VK_IMAGE_TYPE_2D
	VkImageView storageView = VK_NULL_HANDLE; // VK_IMAGE_TYPE_2D_ARRAY
	std::vector<VkImageView> storageViews{};
	bool perMipStorageViews = false;
	VkFormat imageFormat = VK_FORMAT_UNDEFINED;
	VkExtent3D imageExtent{};
	uint32_t mipLevelCount = 0;
	uint32_t arrayLayers = 1;

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

// Total values in memory
// TODO: Utilize this more effectively to hold more values and support future dynamic updates
struct ResourceStats {
	uint32_t totalVertexCount = 0;
	uint32_t totalIndexCount = 0;
	uint32_t totalMaterialCount = 0;
	uint32_t totalMeshCount = 0;
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
	Mesh     // Mesh shader-based
};

struct PipelineObj {
	VkPipeline pipeline;
	MaterialPass pass;
	PipelineCategory type;
};

struct PipelinePresent {
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

	bool enableBlending = false;
	bool enableDepthTest = true;
	bool enableDepthWrite = true;

	VkPolygonMode polygonMode;
	VkPrimitiveTopology topology;
	VkCompareOp depthCompareOp;
	VkCullModeFlagBits cullMode;
	VkFrontFace frontFace;

	std::vector<ShaderStageInfo> shaderStagesInfo;
	VkFormat colorFormat;
	VkFormat depthFormat;
};

struct AllocatedBuffer {
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceAddress address = UINT64_MAX;
	VmaAllocation allocation{};
	VmaAllocationInfo info{};
	void* mapped = nullptr;
};

struct alignas(16) CullingPushConstantsAddrs {
	glm::vec4 frusPlanes[6];
	uint64_t meshIDBufferAddr;
	uint64_t visibleMeshOutBufferAddr;
	glm::vec4 frusPoints[8];
	uint64_t visibleCountOutBufferAddr;
	uint32_t meshCount;
	uint32_t rebuildTransforms;
};
static_assert(sizeof(CullingPushConstantsAddrs) == 256);

struct BakedInstance {
	GPUInstance instance;
	uint32_t gltfMeshIndex = UINT32_MAX;
	uint32_t gltfPrimitiveIndex = UINT32_MAX;
	MaterialPass passType = MaterialPass::Opaque;
	uint32_t nodeID = UINT32_MAX;
};

// Per model
struct UploadMeshContext {
	std::vector<uint32_t> globalIndices;
	std::vector<Vertex> globalVertices;
};

using MeshID = uint32_t;
struct MeshRegistry {
	std::vector<GPUMeshData> meshData;

	// holds a linear list of meshIDs for gpu access
	AllocatedBuffer meshIDBuffer;

	inline std::vector<MeshID> extractAllMeshIDs() const {
		std::vector<MeshID> ids;
		ids.reserve(meshData.size());

		for (MeshID id = 0; id < meshData.size(); ++id) {
			ids.push_back(id);
		}

		return ids;
	}

	inline MeshID registerMesh(const GPUMeshData& data) {
		MeshID id = static_cast<MeshID>(meshData.size());
		ASSERT(id != std::numeric_limits<MeshID>::max() && "MeshRegistry: MeshID overflow!");

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

struct DescriptorsCentral {
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
};