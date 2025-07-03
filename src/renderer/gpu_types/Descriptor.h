#pragma once

#include <utils/BufferUtils.h>
#include <common/Vk_Types.h>

struct PoolSizeRatio {
	VkDescriptorType type;
	float ratio = 0.0f;
};

struct PendingWrite {
	uint32_t binding;
	VkDescriptorType type;
	size_t startIndex;
	size_t count;
	VkDescriptorSet dstSet;
};

struct DescriptorWriter {
	std::vector<VkDescriptorImageInfo> imageInfos;
	std::vector<VkWriteDescriptorSet> imageWrites;
	std::vector<PendingWrite> pendingImageWrites;

	std::vector<VkDescriptorBufferInfo> bufferInfos;
	std::vector<VkWriteDescriptorSet> bufferWrites;
	std::vector<size_t> writeBufferIndices;

	void writeFromImageLUT(const std::vector<ImageLUTEntry>& lut, const ImageTable& table, VkDescriptorSet descriptorSet);

	void writeBuffer(int binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type, VkDescriptorSet set);
	void writeImages(int binding, const std::vector<VkDescriptorImageInfo>& images, VkDescriptorType type, VkDescriptorSet set);

	void clear();
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