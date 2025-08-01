#pragma once

#include "common/EngineTypes.h"
#include "common/ResourceTypes.h"
#include "common/EngineConstants.h"

static uint32_t CURRENT_MSAA_LVL = MSAACOUNT_8;
static bool MSAA_ENABLED = true;

struct GPUResources {
public:
	VmaAllocator& getAllocator() { return allocator; }
	DeletionQueue& getMainDeletionQueue() { return mainDeletionQueue; } // program life use
	DeletionQueue& getTempDeletionQueue() { return tempDeletionQueue; } // temp data and deferred deletion
	VkCommandPool& getGraphicsPool() { return graphicsPool; }
	VkCommandPool& getTransferPool() { return transferPool; }
	VkCommandPool& getComputePool() { return computePool; }
	VkFence& getLastSubmittedFence() { return lastSubmittedFence; }
	void init(VkDevice device);

	GPUAddressTable& getAddressTable() { return gpuAddresses; }
	AllocatedBuffer& getAddressTableBuffer() { return addressTableBuffer; }

	AllocatedBuffer& getGPUAddrsBuffer(AddressBufferType type) { return gpuBuffers.at(type); }
	void addGPUBufferToGlobalAddress(AddressBufferType addressBufferType, AllocatedBuffer gpuBuffer);
	void clearAddressBuffer(AddressBufferType type) { gpuBuffers.erase(type); }

	// Table is marked dirty if a gpu address is updated, returns to clean afterward
	// Setting force to 'true' will update the table without having to directly update the individual addresses,
	// like extending a current address
	void updateAddressTableMapped(VkCommandPool transferCommandPool, bool force = false);

	// All submesh access
	// Maps meshes to their vertex/index buffer regions for indirect drawing
	std::vector<GPUDrawRange>& getDrawRanges() { return drawRanges; }
	MeshRegistry& getResgisteredMeshes() { return registeredMeshes; }

	ImageLUTManager& getLUTManager(ImageLUTType type) {
		auto it = lutManagers.find(type);
		ASSERT(it != lutManagers.end() && "Unknown ImageLUTType in getLUTManager()");
		return *(it->second);
	}

	void addImageLUTEntry(ImageLUTType type, const ImageLUTEntry& entry) {
		getLUTManager(type).addEntry(entry);
	}

	void clearLUTEntries(ImageLUTType type) {
		getLUTManager(type).clear();
	}

	ResourceStats stats;

	AllocatedBuffer envMapSetUBO;

	void cleanup(VkDevice device);

private:
	GPUAddressTable gpuAddresses;
	AllocatedBuffer addressTableBuffer; // descriptor written buffer, mapped from gpuaddresses
	AllocatedBuffer addressTableStagingBuffer;
	mutable std::mutex addressTableMutex;

	MeshRegistry registeredMeshes;

	bool addressTableDirty = false;
	void markAddressTableDirty() {
		addressTableDirty = true;
	}

	std::unordered_map<ImageLUTType, std::unique_ptr<ImageLUTManager>> lutManagers;

	std::vector<GPUDrawRange> drawRanges;

	std::unordered_map<AddressBufferType, AllocatedBuffer> gpuBuffers{};

	VmaAllocator allocator = nullptr;
	DeletionQueue mainDeletionQueue;
	DeletionQueue tempDeletionQueue;

	// Graphics work
	VkCommandPool graphicsPool = VK_NULL_HANDLE;
	VkCommandPool transferPool = VK_NULL_HANDLE;
	VkCommandPool computePool = VK_NULL_HANDLE;
	VkFence lastSubmittedFence = VK_NULL_HANDLE;
};

namespace ResourceManager {
	extern ImageTableManager _globalImageManager; // static images
	//extern ImageTableManager _materialTextureManager; // per frame based
	extern GPUEnvMapIndices _envMapIndices;

	AllocatedImage& getDrawImage();
	AllocatedImage& getDepthImage();
	AllocatedImage& getMSAAImage();
	AllocatedImage& getToneMappingImage();
	extern ColorData toneMappingData;
	std::vector<VkSampleCountFlags>& getAvailableSampleCounts();
	void initRenderImages(DeletionQueue& queue, const VmaAllocator allocator, const VkExtent3D drawExtent);

	AllocatedImage& getMetalRoughImage();
	AllocatedImage& getWhiteImage();
	AllocatedImage& getEmissiveImage();
	AllocatedImage& getAOImage();
	AllocatedImage& getNormalImage();
	AllocatedImage& getCheckboardTex();
	VkSampler getDefaultSamplerLinear();
	VkSampler getDefaultSamplerNearest();
	void initTextures(VkCommandPool cmdPool, DeletionQueue& imageQueue, DeletionQueue& bufferQueue, const VmaAllocator allocator);


	AllocatedImage& getSkyBoxImage();
	AllocatedImage& getIrradianceImage();
	AllocatedImage& getSpecularPrefilterImage();
	AllocatedImage& getBRDFImage();
	VkSampler& getBRDFSampler();
	VkSampler& getSpecularPrefilterSampler();
	VkSampler& getIrradianceSampler();
	VkSampler& getSkyBoxSampler();
	void initEnvironmentImages(DeletionQueue& queue, const VmaAllocator allocator);
}