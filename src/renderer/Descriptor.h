#pragma once

//#include <glm/glm.hpp>
//#include <glm/gtc/matrix_transform.hpp>
#include <chrono>
#include <utils/BufferUtils.h>
#include <common/Vk_Types.h>

// holds all descriptor set info needed with its information packed up
// all that is needed to be defined is the descriptorinfo for use
// always initialize the DescriptorInfo since it holds stage and binding info
struct DescriptorsCentral {
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
	DescriptorInfo descriptorInfo{};
	bool enableDescriptorsSetAndLayout = true; // on by default
};

struct DescriptorManager {
	std::vector<VkDescriptorSetLayoutBinding> _bindings;

	void addBinding(uint32_t binding, VkDescriptorType type);
	void clear();
	VkDescriptorSetLayout createSetLayout(VkShaderStageFlags shaderStages, void* pNext = nullptr, VkDescriptorSetLayoutCreateFlags flags = 0);

	struct PoolSizeRatio {
		VkDescriptorType type;
		float ratio;
	};

	VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
	void destroyDescriptorPool();
	void clearDescriptors();

	void createDescriptorPool(uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);
	VkDescriptorSet allocateDescriptor(VkDescriptorSetLayout layout);

	void createDescriptors(DescriptorsCentral& descriptors, DeletionQueue& deletionQueue);
};

namespace DescriptorSetOverwatch {
	extern DescriptorManager descriptorManager;

	DescriptorsCentral& getMeshesDescriptors();

	DescriptorsCentral& getDrawImageDescriptors();

	// backend calls once
	void initAllDescriptors();
}

struct UniformBufferObject {
	alignas(16) glm::mat4 model;
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 proj;
};