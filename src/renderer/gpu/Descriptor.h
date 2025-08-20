#pragma once

#include <common/ResourceTypes.h>
#include <common/EngineTypes.h>

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
	DescriptorsCentral& getUnifiedDescriptors();
	DescriptorsCentral& getFrameDescriptors();
	void initDescriptors(const VkDevice device, DeletionQueue& queue);
}