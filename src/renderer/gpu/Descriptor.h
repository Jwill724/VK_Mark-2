#pragma once

#include <common/EngineTypes.h>
#include <common/ResourceTypes.h>

struct DescriptorsCentral {
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
};

struct PoolSizeRatio {
	VkDescriptorType type;
	float ratio = 0.0f;
};

struct DescriptorWriteGroup {
	uint32_t binding = UINT32_MAX;
	VkDescriptorType type{};
	VkDescriptorSet dstSet = VK_NULL_HANDLE;

	std::vector<VkDescriptorImageInfo> v_imageInfos;
	VkDescriptorImageInfo imageInfo;
};

enum class DescriptorImageType : uint8_t {
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

	bool enablePushDescriptor = false;

	void updatePushSet(
		VkCommandBuffer cmd,
		VkPipelineBindPoint bindPoint,
		VkPipelineLayout pipelineLayout);

	void writeFromImageLUT(const std::vector<ImageLUTEntry>& lut, const ImageTable& table);

	void writePushBuffer(
		uint32_t binding,
		VkBuffer buffer,
		size_t size,
		size_t offset,
		VkDescriptorType type);

	void writeBuffer(
		uint32_t binding,
		VkBuffer buffer,
		size_t size,
		size_t offset,
		VkDescriptorType type,
		VkDescriptorSet set);
	void writeImages(
		uint32_t binding,
		DescriptorImageType type,
		VkDescriptorSet set);

	// sampler == read only, combined sampler type
	// !sampler == storage, general type
	// Optional manual layout definition
	void writePushImage(
		uint32_t binding,
		VkImageView view,
		VkSampler sampler = VK_NULL_HANDLE,
		VkImageLayout layoutOverride = VK_IMAGE_LAYOUT_UNDEFINED);

	// Requires immediate update
	void writeInlineUniform(
		uint32_t binding,
		const void* data,
		uint32_t size,
		VkDevice device,
		VkDescriptorSet set);
	~DescriptorWriter() { clear(); }

	void updateSet(VkDevice device, VkDescriptorSet set);

private:
	// After a set is updated, all data written is cleared
	bool shouldClearWrites;
	void clear();
};

struct DescriptorManager {
	std::vector<VkDescriptorSetLayoutBinding> _bindings;

	std::vector<PoolSizeRatio> ratios;
	std::vector<VkDescriptorPool> fullPools;
	std::vector<VkDescriptorPool> readyPools;
	uint32_t setsPerPool = 0;

	VkDescriptorPool getPool(const VkDevice device);
	void destroyPools(const VkDevice device);
	VkDescriptorPool createDescriptorPool(const VkDevice device, uint32_t setCount, std::span<PoolSizeRatio> poolRatios);
	VkDescriptorSetLayout createSetLayout(const VkDevice device);
	VkDescriptorSet allocateDescriptor(const VkDevice device,
		VkDescriptorSetLayout layout, void* pNext = nullptr, uint32_t count = 1, bool useVariableCount = false);
	void clearPools(const VkDevice device);
	void addBinding(uint32_t binding, VkDescriptorType type, VkShaderStageFlags stageFlags, uint32_t count);
	void clearBinding();

	VkDescriptorSetLayout createPushSetLayout(const VkDevice device);

	void init(const VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);
};

namespace DescriptorSetOverwatch {
	extern DescriptorManager mainDescriptorManager;
	DescriptorsCentral& getUnifiedDescriptor();
	DescriptorsCentral& getFrameDescriptor();
	DescriptorsCentral& getPushDescriptor();
	void initDescriptors(const VkDevice device, DeletionQueue& queue);
}