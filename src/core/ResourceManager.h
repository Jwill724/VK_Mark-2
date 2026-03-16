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

	// TODO: Should change the naming style to remove -Image
	AllocatedImage& getOpaqueImage();
	AllocatedImage& getTransparentImage();
	AllocatedImage& getToneMapImage();
	AllocatedImage& getDepthResolvedImage();
	AllocatedImage& getDepthImage();
	AllocatedImage& getPrevDepthResolvedImage();
	AllocatedImage& getNormalImage();
	AllocatedImage& getAORawImage();
	AllocatedImage& getAOTempImage();
	AllocatedImage& getAOHistoryRead();
	AllocatedImage& getAOHistoryWrite();
	void flipAOHistory();
	void resetAOHistoryIndex();
	AllocatedImage& getBounceLightHistoryRead();
	AllocatedImage& getBounceLightHistoryWrite();
	void flipBounceLightHistory();
	void resetBounceLightHistoryIndex();
	AllocatedImage& getFlareBrightImage();
	AllocatedImage& getLensFlareColorImage();
	AllocatedImage& getRainbowLUTImage();
	AllocatedImage& getVelocityImage();
	AllocatedImage& getVolumetricLightImage();
	AllocatedImage& getVolumetricBlurImage();
	AllocatedImage& getDirectionalCSMAtlas();
	AllocatedImage& getScreenSpaceShadowMask();
	AllocatedImage& getBentNormalsImage();
	AllocatedImage& getHiZ();
	AllocatedImage& getEdgeInfoImage();
	AllocatedImage& getAOFinalImage();
	AllocatedImage& getAAColor();
	AllocatedImage& getCMAA2WorkingEdges();
	AllocatedImage& getSMAAEdges();
	AllocatedImage& getSMAAWeights();
	AllocatedImage& getFlashLightShadowMap();
	AllocatedImage& getCookieGoboImage();
	AllocatedImage& getAreaTex();
	AllocatedImage& getSearchTex();
	const VkSampler getNearestClampSampler();
	const VkSampler getLinearClampSampler();
	const VkSampler getHiZSampler();
	const VkSampler getLinearLODClampSampler();
	const VkSampler getPointBorderSampler();
	const VkSampler getNoiseSampler();
	const VkSampler getShadowMapSampler();

	// Empty black image
	AllocatedImage& getDummyImage();

	//AllocatedImage& get4x4NoiseImage();

	//std::vector<VkDescriptorSet>& getShadowMapDescriptors();

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

	AllocatedImage& getMetalRoughMat();
	AllocatedImage& getWhiteMat();
	AllocatedImage& getEmissiveMat();
	AllocatedImage& getAOMat();
	AllocatedImage& getNormaMat();
	AllocatedImage& getCheckboardTex();
	AllocatedImage& getDummyUint8();
	const VkSampler getDefaultSamplerLinear();
	const VkSampler getDefaultSamplerNearest();
	void initTextures(
		const VkDevice device,
		VkCommandPool cmdPool,
		DeletionQueue& imageQueue,
		DeletionQueue& bufferQueue,
		const VmaAllocator allocator);

	AllocatedImage& getBRDFImage();
	const VkSampler getBRDFSampler();
	const VkSampler getSpecularPrefilterSampler();
	const VkSampler getIrradianceSampler();
	const VkSampler getSkyBoxSampler();

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
