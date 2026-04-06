#pragma once

#include "common/EngineTypes.h"
#include "common/ResourceTypes.h"
#include "common/EngineConstants.h"

// Anisotropic Filtering
inline float CURRENT_AF_LVL = ANISOTROPY_LEVEL_8;

struct GPUResources {
public:
	VmaAllocator& getAllocator() { return allocator; }
	DeletionQueue& getMainDQueue() { return mainDeletionQueue; }
	DeletionQueue& getTempDQueue() { return tempDeletionQueue; }
	DeletionQueue& getRenderTargetDQueue() { return renderTargetQueue; }
	DeletionQueue& getDynamicPipelineQueue() { return dynamicPipelineQueue; }
	DeletionQueue& getDynamicPipelineShaderStagesQueue() { return dynamicPipelineShaderStagesQueue; }
	VkCommandPool& getGraphicsPool() { return graphicsPool; }
	VkCommandPool& getTransferPool() { return transferPool; }
	VkCommandPool& getComputePool() { return computePool; }
	VkFence& getLastSubmittedFence() { return lastSubmittedFence; }

	std::vector<uint32_t>& getMaterialFlagsByID() { return materialFlagsIDs; }

	void init(const VkDevice device);

	GPUAddressTable& getAddressTable() { return gpuAddresses; }
	AllocatedBuffer& getAddressTableBuffer() { return addressTableBuffer; }

	AllocatedBuffer& getGPUAddrsBuffer(AddressBufferType type) { return gpuBuffers.at(type); }
	bool containsGPUBuffer(AddressBufferType type) const {
		auto it = gpuBuffers.find(type);
		return it != gpuBuffers.end() && it->second.buffer != VK_NULL_HANDLE;
	}
	void addGPUBufferToGlobalAddress(AddressBufferType addressBufferType, AllocatedBuffer gpuBuffer);
	void clearAddressBuffer(AddressBufferType type) { gpuBuffers.erase(type); }

	// Marked dirty whenever new addresses are added and clean when this function finishes its upload.
	// This is designed for the global address table only.
	void updateAddressTableMapped();

	// All submesh access
	// Maps meshes to their vertex/index buffer regions for indirect drawing
	MeshRegistry& getResgisteredMeshes() { return registeredMeshes; }

	ImageLUTManager& getLUTManager() { return lutManager; }

	void addImageLUTEntry(const ImageLUTEntry& entry) {
		getLUTManager().addEntry(entry);
	}

	void clearLUTEntries() {
		getLUTManager().clear();
	}

	AllocatedBuffer& getLightListStagingBuffer() {
		return lightListStagingBuffer;
	}

	AllocatedBuffer& getInstanceTransformsStagingBuffer() {
		return instanceTransformsStagingBuffer;
	}

	ModelDataCounts modelDataCounts;

	// Uniform buffers
	AllocatedBuffer envMapIndexBuffer;

	void cleanup(VkDevice device);

	bool assetsLoaded = false;

	// stores search and area lut texture ids
	BindlessAccessPush smaaTextures;

private:
	GPUAddressTable gpuAddresses{};
	AllocatedBuffer addressTableBuffer; // descriptor written buffer, mapped from gpuaddresses
	AllocatedBuffer addressTableStagingBuffer;
	mutable std::mutex addressTableMutex;

	AllocatedBuffer lightListStagingBuffer;

	AllocatedBuffer instanceTransformsStagingBuffer;

	MeshRegistry registeredMeshes;

	ImageLUTManager lutManager{};

	std::vector<uint32_t> materialFlagsIDs;

	std::unordered_map<AddressBufferType, AllocatedBuffer> gpuBuffers{};

	VmaAllocator allocator = nullptr;
	DeletionQueue mainDeletionQueue;                // Runtime static
	DeletionQueue tempDeletionQueue;                // Primary use in asset loading prep
	DeletionQueue renderTargetQueue;                // For when new extents occur

	DeletionQueue dynamicPipelineShaderStagesQueue;
	DeletionQueue dynamicPipelineQueue;

	// Graphics work
	VkCommandPool graphicsPool = VK_NULL_HANDLE;
	VkCommandPool transferPool = VK_NULL_HANDLE;
	VkCommandPool computePool = VK_NULL_HANDLE;
	VkFence lastSubmittedFence = VK_NULL_HANDLE;
};

namespace ResourceManager {
	extern ImageTableManager _globalImageManager;
	extern EnvironmentSet _environmentSets[MAX_ENV_SETS];
	extern GPUEnvMapIndexArray _envMapIdxArray;

	extern glm::vec4 _luminanceSums[MAX_LUMINANCE_GROUPS];

	AllocatedImage& getOpaque_Target();
	AllocatedImage& getTransparentResolved_Target();
	// For OIT
	AllocatedImage& getTransparentAccumulation_Target();
	AllocatedImage& getTransparentRevealage_Target();

	AllocatedImage& getToneMap_Target();

	// Pre pass depth
	AllocatedImage& getDepthResolved_Target();
	AllocatedImage& getPrevDepthResolved_Target();
	AllocatedImage& getHiZ_Target(); // r32uint packed min/max
	AllocatedImage& getLinearizedMinHiZ_Target(); // r32f linearized min

	// An empty base depth
	AllocatedImage& getDepthRaw_Target();

	AllocatedImage& getAORaw_Target();
	AllocatedImage& getAOTemp_Target();

	AllocatedImage& getColorHistoryRead_Target();
	AllocatedImage& getColorHistoryWrite_Target();
	void flipColorHistory();
	void resetColorHistoryIndex();

	AllocatedImage& getFlareBright_Target();
	AllocatedImage& getLensFlareColor_Target();
	AllocatedImage& getVelocity_Target();
	AllocatedImage& getPrevVelocity_Target();
	AllocatedImage& getViewSpaceNormals_Target();
	AllocatedImage& getVolumetricLight_Target();
	AllocatedImage& getVolumetricBlur_Target();
	AllocatedImage& getDirectionalCSMAtlas_Target();
	AllocatedImage& getScreenSpaceShadowMask_Target();
	AllocatedImage& getBentNormals_Target();
	AllocatedImage& getAOEdgeInfo_Target();
	AllocatedImage& getAAColor_Target();
	AllocatedImage& getPostNonAAComposite_Target();
	AllocatedImage& getCMAA2WorkingEdges_Target();
	AllocatedImage& getSMAAEdges_Target();
	AllocatedImage& getSMAAWeights_Target();
	AllocatedImage& getFlashLightShadowMap_Target();

	AllocatedImage& getRainbowLUT_Texture();
	AllocatedImage& getCookieGobo_Texture();
	AllocatedImage& getAreaSMAA_Texture();
	AllocatedImage& getSearchSMAA_Texture();

	const VkSampler getNearestClamp_Sampler();
	const VkSampler getLinearClamp_Sampler();
	const VkSampler getHiZ_Sampler();
	const VkSampler getLinearLODClamp_Sampler();
	const VkSampler getPointBorder_Sampler();
	const VkSampler getTaaHistory_Sampler();
	const VkSampler getNoise_Sampler();
	const VkSampler getShadowMap_Sampler();

	// Empty black image
	AllocatedImage& getDummy_Texture();

	void initRenderSamplers(
		const VkDevice device,
		DeletionQueue& queue);

	void initShadowMapImages(
		const VkDevice device,
		DeletionQueue& queue,
		const VmaAllocator allocator);

	void initUniformRenderTargets(
		const VkDevice device,
		DeletionQueue& targetQueue,
		const VmaAllocator allocator,
		const VkExtent3D drawExtent);

	AllocatedImage& getMetalRough_Texture();
	AllocatedImage& getWhiteMat_Texture();
	AllocatedImage& getEmissive_Texture();
	AllocatedImage& getAO_Texture();
	AllocatedImage& getNormal_Texture();
	AllocatedImage& getCheckboard_Texture();
	AllocatedImage& getDummyUint8_Texture();
	const VkSampler getDefaultLinear_Sampler();
	const VkSampler getDefaultNearest_Sampler();
	void initTextures(
		const VkDevice device,
		VkCommandPool cmdPool,
		DeletionQueue& imageQueue,
		DeletionQueue& bufferQueue,
		const VmaAllocator allocator);

	AllocatedImage& getBRDF_Texture();
	const VkSampler getBRDF_Sampler();
	const VkSampler getSpecularPrefilter_Sampler();
	const VkSampler getIrradiance_Sampler();
	const VkSampler getSkyBox_Sampler();

	EnvironmentSet initEnvironmentSetImages(
		const VkDevice device,
		DeletionQueue& queue,
		const VmaAllocator allocator);

	// Defines samplers and the brdf
	void initStaticEnvironmentImages(
		const VkDevice device,
		DeletionQueue& queue,
		const VmaAllocator allocator);
}
