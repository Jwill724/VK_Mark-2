#pragma once

#include "Vk_Types.h"
#include "fmt/base.h"

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
};

struct AllocatedImage {
	VkImage image = VK_NULL_HANDLE;
	VkImageView imageView = VK_NULL_HANDLE;
	VkFormat imageFormat = VK_FORMAT_UNDEFINED;
	VmaAllocation allocation = nullptr;
	VkExtent3D imageExtent;
	ImageLUTEntry lutEntry{};
	std::vector<VkImageView> storageViews; // Used for sampling on different image views
	uint32_t mipLevelCount = 0;
	bool mipmapped = false;
	bool isCubeMap = false;
};

struct ImageTable {
	std::mutex combinedMutex;
	std::mutex storageMutex;
	std::mutex samplerCubeMutex;
	std::vector<VkDescriptorImageInfo> combinedViews;
	std::vector<VkDescriptorImageInfo> storageViews;
	std::vector<VkDescriptorImageInfo> samplerCubeViews;

	std::unordered_map<ImageViewSamplerKey, uint32_t, HashPair, EqualPair> combinedViewHashToID;
	std::unordered_map<ImageViewSamplerKey, uint32_t, HashPair, EqualPair> samplerCubeViewHashToID;
	std::unordered_map<size_t, uint32_t> storageViewHashToID;

	uint32_t pushCombined(VkImageView view, VkSampler sampler);
	uint32_t pushStorage(VkImageView view);
	uint32_t pushSamplerCube(VkImageView view, VkSampler sampler);

	inline void clearTables() {
		combinedViews.clear();
		storageViews.clear();
		samplerCubeViews.clear();
	}

	static inline ImageViewSamplerKey makeKey(VkImageView view, VkSampler sampler) {
		return { view, sampler };
	}
};

inline uint32_t ImageTable::pushCombined(VkImageView view, VkSampler sampler) {
	assert(view != VK_NULL_HANDLE && sampler != VK_NULL_HANDLE && "Null handle in pushCombined");

	ImageViewSamplerKey key = makeKey(view, sampler);

	std::scoped_lock lock(combinedMutex);

	auto it = combinedViewHashToID.find(key);
	if (it != combinedViewHashToID.end())
		return it->second;

	VkDescriptorImageInfo info{};
	info.imageView = view;
	info.sampler = sampler;
	info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	fmt::print("[ImageTable::pushCombined] New entry: view={}, sampler={}, layout=0x{:08X}\n",
		(void*)view, (void*)sampler, static_cast<uint32_t>(info.imageLayout));

	uint32_t index = static_cast<uint32_t>(combinedViews.size());
	combinedViews.push_back(info);
	combinedViewHashToID[key] = index;
	fmt::print("Index: {}\n", index);

	return index;
}

inline uint32_t ImageTable::pushSamplerCube(VkImageView view, VkSampler sampler) {
	assert(view != VK_NULL_HANDLE && sampler != VK_NULL_HANDLE && "Null handle in pushSamplerCube");

	ImageViewSamplerKey key = makeKey(view, sampler);

	std::scoped_lock lock(samplerCubeMutex);

	auto it = samplerCubeViewHashToID.find(key);
	if (it != samplerCubeViewHashToID.end())
		return it->second;

	VkDescriptorImageInfo info{};
	info.imageView = view;
	info.sampler = sampler;
	info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	fmt::print("[ImageTable::pushSamplerCube] New entry: view={}, sampler={}, layout=0x{:08X}\n",
		(void*)view, (void*)sampler, static_cast<uint32_t>(info.imageLayout));

	uint32_t index = static_cast<uint32_t>(samplerCubeViews.size());
	samplerCubeViews.push_back(info);
	samplerCubeViewHashToID[key] = index;
	fmt::print("Index: {}\n", index);

	return index;
}

inline uint32_t ImageTable::pushStorage(VkImageView view) {
	if (view == VK_NULL_HANDLE) {
		fmt::print("[ImageTable::pushStorage] ERROR: Invalid handle - view={}\n",
			(void*)view);
		assert(false && "Null VkImageView in pushStorage");
	}

	std::scoped_lock lock(storageMutex);

	size_t hash = std::hash<std::uintptr_t>{}(reinterpret_cast<std::uintptr_t>(view));

	auto it = storageViewHashToID.find(hash);
	if (it != storageViewHashToID.end())
		return it->second;

	VkDescriptorImageInfo info{};
	info.imageView = view;
	info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	info.sampler = VK_NULL_HANDLE;

	fmt::print("[ImageTable::pushStorage] New entry: view={}, layout=0x{:08X}\n",
		(void*)view, static_cast<uint32_t>(info.imageLayout));

	uint32_t index = static_cast<uint32_t>(storageViews.size());
	storageViews.push_back(info);
	storageViewHashToID[hash] = index;
	fmt::print("Index: {}\n", index);

	return index;
}

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

// Final rendering data for recording
struct ComputeEffect {
	const char* name;

	VkPipeline pipeline;

	std::vector<uint8_t> pushData;

	template<typename T>
	void setPushData(const T& value) {
		pushData.resize(sizeof(T));
		std::memcpy(pushData.data(), &value, sizeof(T));
	}

	template<typename T>
	T& getTypedPushData() {
		assert(sizeof(T) == pushData.size());
		return *reinterpret_cast<T*>(pushData.data());
	}

	size_t getPushDataSize() const { return pushData.size(); }
};

struct ShaderStageInfo {
	VkShaderStageFlagBits stage;
	const char* shaderName;
	const char* filePath;
};

struct DescriptorInfo {
	VkDescriptorType type;
	uint32_t binding = 0;
	VkShaderStageFlags stageFlags;

	void* pNext = nullptr;
};

struct GraphicsPipeline {
	VkPipeline pipeline;
	MaterialPass passType;
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

enum class PipelineType {
	Opaque,
	Transparent,
	Wireframe,
	BoundingBoxes
};

struct ComputePipeline {
	ComputeEffect& getComputeEffect() { return computeEffects[currentEffect]; }
	std::vector<ComputeEffect> computeEffects;
	uint32_t currentEffect{ 0 };
};

struct AllocatedBuffer {
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceAddress address{};
	VmaAllocation allocation{};
	VmaAllocationInfo info{};
	void* mapped = nullptr;
};

struct MaterialHandle {
	std::shared_ptr<InstanceData> instance;
	MaterialPass passType;
};

struct MeshHandle {
	std::string name;
	std::vector<std::shared_ptr<MaterialHandle>> materialHandles;
};

struct UploadedMeshView {
	std::span<Vertex> vertexData;
	std::span<uint32_t> indexData;

	uint32_t drawRangeIndex = UINT32_MAX;
};

struct UploadMeshContext {
	std::vector<uint32_t> globalIndices;
	std::vector<Vertex> globalVertices;

	std::vector<std::shared_ptr<MeshHandle>> meshHandles;

	std::vector<UploadedMeshView> meshViews;
};

struct MaterialResources {
	AllocatedImage colorImage;
	VkSampler colorSampler;

	AllocatedImage metalRoughImage;
	VkSampler metalRoughSampler;

	AllocatedImage aoImage;
	VkSampler aoSampler;

	AllocatedImage normalImage;
	VkSampler normalSampler;

	AllocatedImage emissiveImage;
	VkSampler emissiveSampler;

	VkBuffer dataBuffer;
	size_t dataBufferOffset;
	void* dataBufferMapped;
};


struct DescriptorsCentral {
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
};


struct DrawContext;

// base class for a renderable dynamic object
class IRenderable {

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) = 0;
};

// implementation of a drawable scene node.
// the scene node can hold children and will also keep a transform to propagate
// to them
struct Node : public IRenderable {

	// parent pointer must be a weak pointer to avoid circular dependencies
	std::weak_ptr<Node> parent;
	std::vector<std::shared_ptr<Node>> children;

	glm::mat4 localTransform;
	glm::mat4 worldTransform;

	void refreshTransform(const glm::mat4& parentMatrix)
	{
		worldTransform = parentMatrix * localTransform;
		for (auto c : children) {
			c->refreshTransform(worldTransform);
		}
	}

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx)
	{
		// draw children
		for (auto& c : children) {
			c->Draw(topMatrix, ctx);
		}
	}
};

struct MeshNode : public Node {

	std::shared_ptr<MeshHandle> mesh;

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) override;
};