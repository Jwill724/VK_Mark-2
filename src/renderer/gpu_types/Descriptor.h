#pragma once

#include <common/Vk_Types.h>
#include <common/ResourceTypes.h>
#include <common/EngineTypes.h>

struct PoolSizeRatio {
	VkDescriptorType type;
	float ratio = 0.0f;
};

struct DescriptorWriteGroup {
	uint32_t binding = UINT32_MAX;
	VkDescriptorType type{};
	VkDescriptorSet dstSet = VK_NULL_HANDLE;

	std::vector<VkDescriptorImageInfo> imageInfos;
};

enum class DescriptorImageType {
	SamplerCube,
	StorageImage,
	CombinedSampler
};

struct DescriptorWriter {
	// Per-binding grouped image descriptor writes
	std::vector<DescriptorWriteGroup> imageWriteGroups;

	std::vector<VkDescriptorBufferInfo> bufferInfos;
	std::vector<VkWriteDescriptorSet> bufferWrites;
	std::vector<size_t> writeBufferIndices;

	std::vector<VkDescriptorImageInfo> samplerCubeDescriptors;
	std::vector<VkDescriptorImageInfo> storageDescriptors;
	std::vector<VkDescriptorImageInfo> combinedDescriptors;

	void writeFromImageLUT(const std::vector<ImageLUTEntry>& lut, const ImageTable& table);

	void writeBuffer(uint32_t binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type, VkDescriptorSet set);
	void writeImages(uint32_t binding, DescriptorImageType type, VkDescriptorSet set);

	void clear();

	~DescriptorWriter() { clear(); }

	void updateSet(VkDevice device, VkDescriptorSet set);
};

struct DescriptorManager {
	std::vector<VkDescriptorSetLayoutBinding> _bindings;

	std::vector<PoolSizeRatio> ratios;
	std::vector<VkDescriptorPool> fullPools;
	std::vector<VkDescriptorPool> readyPools;
	uint32_t setsPerPool = 0;

	VkDescriptorPool getPool();

	void destroyPools();
	VkDescriptorPool createDescriptorPool(uint32_t setCount, std::span<PoolSizeRatio> poolRatios);
	VkDescriptorSetLayout createSetLayout();
	VkDescriptorSet allocateDescriptor(VkDevice device,
		VkDescriptorSetLayout layout, void* pNext = nullptr, uint32_t count = 1, bool useVariableCount = false);
	void clearPools();
	void addBinding(uint32_t binding, VkDescriptorType type, VkShaderStageFlags stageFlags, uint32_t count);
	void clearBinding();

	void init(uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);
};

namespace DescriptorSetOverwatch {
	extern DescriptorManager mainDescriptorManager;
	DescriptorsCentral& getUnifiedDescriptors();
	DescriptorsCentral& getFrameDescriptors();
	void initDescriptors(DeletionQueue& queue);
}