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

struct ImageLUTEntry {
	uint32_t combinedImageIndex = UINT32_MAX;
	uint32_t storageImageIndex = UINT32_MAX;
	uint32_t samplerCubeIndex = UINT32_MAX;
};

struct AllocatedImage {
	VkImage image = VK_NULL_HANDLE;
	VkImageView imageView = VK_NULL_HANDLE;
	VkImageView storageView = VK_NULL_HANDLE;
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
	ImageLUTEntry lutEntry{};

	bool mipmapped = false;
	bool isCubeMap = false;
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
	uint32_t binding = 0;
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

struct RenderInstance {
	std::shared_ptr<GPUInstance> instances;
	uint32_t gltfMeshIndex;
	uint32_t gltfPrimitiveIndex;
	MaterialPass passType;
};

struct UploadedMeshView {
	size_t vertexSizeBytes;
	size_t indexSizeBytes;
};

struct UploadMeshContext {
	std::vector<uint32_t> globalIndices;
	std::vector<Vertex> globalVertices;

	std::vector<UploadedMeshView> meshViews;
};

using MeshID = uint32_t;
struct MeshRegistry {
	std::vector<GPUMeshData> meshData;

	AllocatedBuffer meshIDBuffer{};

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
};

struct DescriptorsCentral {
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
};


struct GPUInstance;

// ====== Interface ======
class IRenderable {
public:
	virtual void FindVisibleObjects(
		std::vector<GPUInstance>& outOpaqueVisibles,
		std::vector<GPUInstance>& outTransparentVisibles,
		const std::unordered_map<uint32_t, std::vector<glm::mat4>>& meshIDToTransformMap,
		const std::unordered_set<uint32_t>& visibleMeshIDSet) = 0;

	virtual ~IRenderable() = default;
};

// ====== Scene Graph Node Base ======
struct Node : public IRenderable {
	std::weak_ptr<Node> parent;
	std::vector<std::shared_ptr<Node>> children;

	glm::mat4 localTransform{};
	glm::mat4 worldTransform{};

	uint32_t nodeIndex = UINT32_MAX;

	void refreshTransform(const glm::mat4& parentMatrix) {
		worldTransform = parentMatrix * localTransform;
		for (auto& c : children) {
			if (c) c->refreshTransform(worldTransform);
		}
	}

	virtual void FindVisibleObjects(
		std::vector<GPUInstance>& outOpaqueVisibles,
		std::vector<GPUInstance>& outTransparentVisibles,
		const std::unordered_map<uint32_t, std::vector<glm::mat4>>& meshIDToTransformMap,
		const std::unordered_set<uint32_t>& visibleMeshIDSet) override
	{
		for (auto& c : children) {
			if (c) c->FindVisibleObjects(outOpaqueVisibles, outTransparentVisibles, meshIDToTransformMap, visibleMeshIDSet);
		}
	}

};

// ====== Mesh Node ======
struct MeshNode : public Node {
	std::vector<std::shared_ptr<RenderInstance>> instances;

	virtual void FindVisibleObjects(
		std::vector<GPUInstance>& outOpaqueVisibles,
		std::vector<GPUInstance>& outTransparentVisibles,
		const std::unordered_map<uint32_t, std::vector<glm::mat4>>& meshIDToTransformMap,
		const std::unordered_set<uint32_t>& visibleMeshIDSet) override;
};