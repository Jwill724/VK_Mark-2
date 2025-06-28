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
	VkFormat imageFormat = VK_FORMAT_UNDEFINED;
	VmaAllocation allocation = nullptr;
	VkExtent3D imageExtent;
	ImageLUTEntry lutEntry{};
	std::vector<VkImageView> storageViews; // Used for sampling on different image views
	uint32_t mipLevelCount = 0;
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

struct SceneObject {
	std::shared_ptr<GPUInstance> instances;
	MaterialPass pass;
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
	std::unordered_map<std::string, MeshID> nameToID;
	std::vector<MeshData> meshData;

	AllocatedBuffer meshIDBuffer{};

	inline std::vector<MeshID> extractAllMeshIDs() {
		std::vector<MeshID> ids;
		ids.reserve(nameToID.size());

		for (const auto& [name, id] : nameToID) {
			ASSERT(id < meshData.size() && "MeshRegistry: MeshID out of bounds!");
			ids.push_back(id);
		}

		return ids;
	}

	inline MeshID registerMesh(const std::string& name, const MeshData& data) {
		ASSERT(!name.empty() && "MeshRegistry: Mesh name must not be empty.");

		if (nameToID.contains(name)) {
			MeshID existingID = nameToID[name];
			ASSERT(existingID < meshData.size() && "MeshRegistry: Existing mesh ID is invalid!");
			return existingID;
		}

		MeshID id = static_cast<MeshID>(meshData.size());
		ASSERT(id != std::numeric_limits<MeshID>::max() && "MeshRegistry: MeshID overflow!");

		meshData.push_back(data);
		nameToID[name] = id;

		ASSERT(meshData.size() == nameToID.size() && "MeshRegistry: nameToID and meshData out of sync!");

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

// base class for a renderable dynamic object
class IRenderable {

	virtual void FlattenSceneToTransformList(const glm::mat4& topMatrix, std::unordered_map<uint32_t, glm::mat4>& meshIDToTransformMap) = 0;
	virtual void FindVisibleObjects(
		std::vector<GPUInstance>& outOpaqueVisibles,
		std::vector<GPUInstance>& outTransparentVisibles,
		const std::unordered_map<uint32_t, glm::mat4>& meshIDToTransformMap,
		const std::unordered_set<uint32_t>& visibleMeshIDSet) = 0;
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
	bool transformDirty = true;

	void markDirty()
	{
		transformDirty = true;
		for (auto& c : children)
			c->markDirty();
	}

	void refreshTransform(const glm::mat4& parentMatrix)
	{
		if (!transformDirty && parent.lock() && worldTransform == parent.lock()->worldTransform * localTransform)
			return;

		worldTransform = parentMatrix * localTransform;
		transformDirty = false;

		for (auto& c : children) {
			c->transformDirty = true;
			c->refreshTransform(worldTransform);
		}
	}

	virtual void FlattenSceneToTransformList(const glm::mat4& topMatrix, std::unordered_map<uint32_t, glm::mat4>& meshIDToTransformMap)
	{
		// draw children
		for (auto& c : children) {
			c->FlattenSceneToTransformList(topMatrix, meshIDToTransformMap);
		}
	}

	virtual void FindVisibleObjects(
		std::vector<GPUInstance>& outOpaqueVisibles,
		std::vector<GPUInstance>& outTransparentVisibles,
		const std::unordered_map<uint32_t, glm::mat4>& meshIDToTransformMap,
		const std::unordered_set<uint32_t>& visibleMeshIDSet)
	{
		for (auto& c : children) {
			c->FindVisibleObjects(outOpaqueVisibles, outTransparentVisibles, meshIDToTransformMap, visibleMeshIDSet);
		}
	}

};

struct MeshNode : public Node {

	std::shared_ptr<SceneObject> objs;

	void FlattenSceneToTransformList(const glm::mat4& topMatrix, std::unordered_map<uint32_t, glm::mat4>& meshIDToTransformMap) override;
	void FindVisibleObjects(
		std::vector<GPUInstance>& outOpaqueVisibles,
		std::vector<GPUInstance>& outTransparentVisibles,
		const std::unordered_map<uint32_t, glm::mat4>& meshIDToTransformMap,
		const std::unordered_set<uint32_t>& visibleMeshIDSet) override;
};