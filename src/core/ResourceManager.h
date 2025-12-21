#pragma once

#include "common/EngineTypes.h"
#include "common/ResourceTypes.h"
#include "common/EngineConstants.h"

// TODO: Find a better place for this.
// All systems that need msaa counts are
// image creation in here, pipeline setup, and during rendering
static uint32_t CURRENT_MSAA_LVL = MSAACOUNT_8;
static bool MSAA_ENABLED = true;

// Anisotropic Filtering
static float CURRENT_AF_LVL = ANISOTROPY_LEVEL_16;

struct GPUResources {
public:
	VmaAllocator& getAllocator() { return allocator; }
	DeletionQueue& getMainDQueue() { return mainDeletionQueue; }
	DeletionQueue& getTempDQueue() { return tempDeletionQueue; }
	DeletionQueue& getRenderTargetDQueue() { return renderTargetQueue; }
	VkCommandPool& getGraphicsPool() { return graphicsPool; }
	VkCommandPool& getTransferPool() { return transferPool; }
	VkCommandPool& getComputePool() { return computePool; }
	VkFence& getLastSubmittedFence() { return lastSubmittedFence; }
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

	ModelDataCounts modelDataCounts;

	// Uniform buffers
	AllocatedBuffer envMapIndexBuffer;
	AllocatedBuffer ssaoKernelBuffer;

	void cleanup(VkDevice device);

	bool assetsLoaded = false;

private:
	GPUAddressTable gpuAddresses{};
	AllocatedBuffer addressTableBuffer; // descriptor written buffer, mapped from gpuaddresses
	AllocatedBuffer addressTableStagingBuffer;
	mutable std::mutex addressTableMutex;

	MeshRegistry registeredMeshes;

	ImageLUTManager lutManager{};

	std::unordered_map<AddressBufferType, AllocatedBuffer> gpuBuffers{};

	VmaAllocator allocator = nullptr;
	DeletionQueue mainDeletionQueue; // Runtime static
	DeletionQueue tempDeletionQueue; // Primary use in asset loading prep
	DeletionQueue renderTargetQueue; // For when new extents occur

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
	extern glm::vec4 _ssaoKernelBlock[KERNEL_BLOCK_SIZE];
	void initSSAOKernel();

	extern glm::vec4 _luminanceSums[MAX_LUMINANCE_GROUPS];

	AllocatedImage& getOpaqueImage();
	AllocatedImage& getTransparentImage();
	AllocatedImage& getDummyTransparent();
	AllocatedImage& getToneMapImage();
	AllocatedImage& getDepthImage();
	AllocatedImage& getMSAAImage();
	AllocatedImage& getDepthResolvedImage();
	AllocatedImage& getPrevDepthResolvedImage();
	AllocatedImage& getNormalImage();
	AllocatedImage& getAORawImage();
	AllocatedImage& getAOTempImage();
	AllocatedImage& getAOHistoryRead();
	AllocatedImage& getAOHistoryWrite();
	void flipAOHistory();
	void resetAOHistoryIndex();
	AllocatedImage& getFlareBrightImage();
	AllocatedImage& getLensFlareColorImage();
	AllocatedImage& getRainbowLUTImage();
	AllocatedImage& getVelocityImage();
	AllocatedImage& getVolumetricLightImage();
	AllocatedImage& getVolumetricBlurImage();
	AllocatedImage& getVolumetricNoiseImage();
	AllocatedImage& get4x4NoiseImage();
	AllocatedImage& getShadowMapImage();
	AllocatedImage& getBentNormalImage();
	AllocatedImage& getDepthPyramidImage();
	AllocatedImage& getMaterialDataImage();
	AllocatedImage& getEdgeInfoImage();
	const VkSampler getNearestClampSampler();
	const VkSampler getLinearClampSampler();
	const VkSampler getDepthPyramidSampler();
	const VkSampler getAOSampler();
	const VkSampler getNoiseSampler();
	const VkSampler getShadowMapSampler();

	//std::vector<VkDescriptorSet>& getShadowMapDescriptors();

	std::vector<VkSampleCountFlags>& getAvailableSampleCounts();
	void initRenderSamplers(
		const VkDevice device,
		DeletionQueue& queue);

	void initShadowMapImages(
		const VkDevice device,
		DeletionQueue& queue,
		const VmaAllocator allocator);

	void initRenderTargets(
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