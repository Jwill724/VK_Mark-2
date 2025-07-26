#pragma once

#include "common/Vk_Types.h"
#include "common/EngineTypes.h"
#include "common/ResourceTypes.h"

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

	// the lut is used in descriptor writing
	// combined image is the primary usage so
	// to minimize threading issues and copying of entries, I'm only tracking dups for combined
	void addImageLUTEntry(const ImageLUTEntry& entry) {
		std::scoped_lock lock(lutMutex);

		bool alreadyAdded = false;

		if (entry.combinedImageIndex != UINT32_MAX && pushedCombinedIndices.insert(entry.combinedImageIndex).second) {
			alreadyAdded = true;
		}
		if (entry.samplerCubeIndex != UINT32_MAX && pushedSamplerCubeIndices.insert(entry.samplerCubeIndex).second) {
			alreadyAdded = true;
		}
		if (entry.storageImageIndex != UINT32_MAX && pushedStorageViewIndices.insert(entry.storageImageIndex).second) {
			alreadyAdded = true;
		}

		if (alreadyAdded)
			imageLUTEntries.push_back(entry);
	}

	uint32_t totalVertexCount = 0;
	uint32_t totalIndexCount = 0;

	AllocatedBuffer envMapSetUBO;

	std::vector<ImageLUTEntry>& getImageLUT() { return imageLUTEntries; }

	void clearLUTEntries() {
		imageLUTEntries.clear();
		pushedCombinedIndices.clear();
		pushedSamplerCubeIndices.clear();
		pushedStorageViewIndices.clear();
	}

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

	std::vector<GPUDrawRange> drawRanges;

	std::unordered_map<AddressBufferType, AllocatedBuffer> gpuBuffers{};

	std::mutex lutMutex;
	std::unordered_set<uint32_t> pushedCombinedIndices;
	std::unordered_set<uint32_t> pushedSamplerCubeIndices;
	std::unordered_set<uint32_t> pushedStorageViewIndices;
	std::vector<ImageLUTEntry> imageLUTEntries{};

	VmaAllocator allocator = nullptr;
	DeletionQueue mainDeletionQueue;
	DeletionQueue tempDeletionQueue;

	// Graphics work
	VkCommandPool graphicsPool = VK_NULL_HANDLE;
	VkCommandPool transferPool = VK_NULL_HANDLE;
	VkCommandPool computePool = VK_NULL_HANDLE;
	VkFence lastSubmittedFence = VK_NULL_HANDLE;
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

	void clearTables() {
		combinedViews.clear();
		storageViews.clear();
		samplerCubeViews.clear();

		combinedViewHashToID.clear();
		storageViewHashToID.clear();
		samplerCubeViewHashToID.clear();
	}

	static ImageViewSamplerKey makeKey(VkImageView view, VkSampler sampler) {
		return { view, sampler };
	}
};

namespace ResourceManager {
	extern ImageTable _globalImageTable;

	extern GPUEnvMapIndices _envMapIndices;

	AllocatedImage& getDrawImage();
	AllocatedImage& getDepthImage();
	AllocatedImage& getMSAAImage();
	AllocatedImage& getToneMappingImage();
	extern ColorData toneMappingData;
	std::vector<VkSampleCountFlags>& getAvailableSampleCounts();
	void initRenderImages(DeletionQueue& queue, const VmaAllocator allocator);

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