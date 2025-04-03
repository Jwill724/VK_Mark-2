#pragma once

#include <utils/BufferUtils.h>
#include <common/Vk_Types.h>

// always initialize the DescriptorInfo since it holds stage and binding info
struct DescriptorsCentral {
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
	DescriptorInfo descriptorInfo{};

	bool enableDescriptorsSetAndLayout = true; // on by default
};

struct PoolSizeRatio {
	VkDescriptorType type;
	float ratio;
};

struct DescriptorWriter {
	std::deque<VkDescriptorImageInfo> imageInfos;
	std::deque<VkDescriptorBufferInfo> bufferInfos;
	std::vector<VkWriteDescriptorSet> writes;

	void writeImage(int binding, VkImageView image, VkSampler sampler, VkImageLayout layout, VkDescriptorType type);
	void writeBuffer(int binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type);

	void clear();
	void updateSet(VkDevice device, VkDescriptorSet set);
};

struct DescriptorManager {
	std::vector<VkDescriptorSetLayoutBinding> _bindings;

	std::vector<PoolSizeRatio> ratios;
	std::vector<VkDescriptorPool> fullPools;
	std::vector<VkDescriptorPool> readyPools;
	uint32_t setsPerPool;

	VkDescriptorPool getPool();

	void destroyPools();
	VkDescriptorPool createDescriptorPool(uint32_t setCount, std::span<PoolSizeRatio> poolRatios);
	VkDescriptorSetLayout createSetLayout(VkShaderStageFlags shaderStages, void* pNext = nullptr, VkDescriptorSetLayoutCreateFlags flags = 0);
	VkDescriptorSet allocateDescriptor(VkDescriptorSetLayout layout, void* pNext = nullptr);
	void clearPools();
	void addBinding(uint32_t binding, VkDescriptorType type);
	void clearBinding();

	void init(uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);

	void setupDescriptorLayout(DescriptorsCentral& descriptors);
};

namespace DescriptorSetOverwatch {
	extern DescriptorManager descriptorManager;

	DescriptorsCentral& getDrawImageDescriptors();

	// backend calls once
	void initDescriptors();
}

struct UniformBufferObject {
	alignas(16) glm::mat4 model;
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 proj;
};