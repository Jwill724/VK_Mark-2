#pragma once

#include "common/Vk_Types.h"
#include "common/EngineTypes.h"
#include "common/ResourceTypes.h"

#include "vulkan/Backend.h"

struct GPUResources {
public:
	VmaAllocator& getAllocator() { return allocator; }
	DeletionQueue& getMainDeletionQueue() { return mainDeletionQueue; } // program life use
	DeletionQueue& getTempDeletionQueue() { return tempDeletionQueue; } // temp data and deferred deletion
	VkCommandPool& getGraphicsPool() { return graphicsPool; }
	VkCommandPool& getTransferPool() { return transferPool; }
	VkFence& getLastSubmittedFence() { return lastSubmittedFence; }
	void init();

	GPUAddressTable& getAddressTable() { return gpuAddresses; }
	AllocatedBuffer& getAddressTableBuffer() { return addressTableBuffer; }

	AllocatedBuffer& getVertexBuffer() { return vertexBuffer; }
	AllocatedBuffer& getIndexBuffer() { return indexBuffer; }

	AllocatedBuffer& getBuffer(AddressBufferType type) { return gpuBuffers.at(type); }
	void addGPUBuffer(AddressBufferType addressBufferType, AllocatedBuffer gpuBuffer);
	void clearAddressBuffer(AddressBufferType type) { gpuBuffers.erase(type); }


	void tryClearAddressBuffer(AddressBufferType type, VmaAllocator allocator);


	// Table is marked dirty if a gpu address is updated, returns to clean afterward
	// Setting force to 'true' will update the table without having to directly update the individual addresses,
	// like extending a current address
	void updateAddressTableMapped(VkCommandPool transferCommandPool, bool force = false);

	// all submesh access
	std::vector<GPUDrawRange>& getDrawRanges() { return drawRanges; }
	uint32_t materialCount; // global

	// the lut is used in descriptor writing
	// combined image is the primary usage so
	// to minimize threading issues and copying of entries, I'm only tracking dups for combined
	void addImageLUTEntry(const ImageLUTEntry& entry) {
		std::scoped_lock lock(lutMutex);

		bool added = false;

		if (entry.combinedImageIndex != UINT32_MAX && pushedCombinedIndices.insert(entry.combinedImageIndex).second) {
			imageLUTEntries.push_back(entry);
			added = true;
		}
		if (entry.samplerCubeIndex != UINT32_MAX && pushedSamplerCubeIndices.insert(entry.samplerCubeIndex).second) {
			if (!added) imageLUTEntries.push_back(entry);
			added = true;
		}
		if (entry.storageImageIndex != UINT32_MAX && pushedStorageViewIndices.insert(entry.storageImageIndex).second) {
			if (!added) imageLUTEntries.push_back(entry);
		}
	}

	std::vector<ImageLUTEntry>& getImageLUT() { return imageLUTEntries; }

	void cleanup(VkDevice device);

private:
	GPUAddressTable gpuAddresses;
	AllocatedBuffer addressTableBuffer; // descriptor written buffer, mapped from gpuaddresses
	AllocatedBuffer addressTableStagingBuffer;
	mutable std::mutex addressTableMutex;

	bool addressTableDirty = false;
	void markAddressTableDirty() {
		addressTableDirty = true;
	}

	AllocatedBuffer vertexBuffer;
	AllocatedBuffer indexBuffer;

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
	VkFence lastSubmittedFence = VK_NULL_HANDLE;
};

namespace ResourceManager {
	extern ImageTable _globalImageTable;

	AllocatedImage& getDrawImage();
	AllocatedImage& getDepthImage();
	AllocatedImage& getMSAAImage();
	AllocatedImage& getPostProcessImage();
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