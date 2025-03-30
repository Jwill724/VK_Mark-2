#include "pch.h"

#include "Descriptor.h"
#include "renderer/Renderer.h"
#include "vulkan/Backend.h"

// TODO: look into descriptor system, kinda scuffed
void DescriptorManager::createDescriptors(DescriptorsCentral& descriptors, DeletionQueue& deletionQueue) {
	std::vector<PoolSizeRatio> sizes = { { descriptors.descriptorInfo.type, 1 } };

	// one pool per descriptor manager
	if (_descriptorPool == VK_NULL_HANDLE) {
		createDescriptorPool(10, sizes);
	}

	addBinding(descriptors.descriptorInfo.binding, descriptors.descriptorInfo.type);
	descriptors.descriptorLayout = createSetLayout(descriptors.descriptorInfo.stageFlags);
	descriptors.descriptorSet = allocateDescriptor(descriptors.descriptorLayout);

	VkWriteDescriptorSet descriptorWrite = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext = descriptors.descriptorInfo.pNext,
		.dstSet = descriptors.descriptorSet,
		.dstBinding = descriptors.descriptorInfo.binding,
		.descriptorCount = 1,
		.descriptorType = descriptors.descriptorInfo.type,
	};

	VkDescriptorImageInfo imgInfo{};
	VkDescriptorBufferInfo bufInfo{};

	switch (descriptors.descriptorInfo.type) {
//	case VK_DESCRIPTOR_TYPE_SAMPLER:
	case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
	case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
		imgInfo.imageView = descriptors.descriptorInfo.imageView;
		imgInfo.imageLayout = descriptors.descriptorInfo.imageLayout;
//		imgInfo.sampler = descriptors.descriptorInfo.sampler;
		descriptorWrite.pImageInfo = &imgInfo;
		break;
	case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
	case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
		bufInfo.buffer = descriptors.descriptorInfo.buffer;
		bufInfo.offset = descriptors.descriptorInfo.offset;
		bufInfo.range = descriptors.descriptorInfo.range;
		descriptorWrite.pBufferInfo = &bufInfo;
		break;
	default:
		throw std::runtime_error("Unsupported descriptor type!");
	}

	vkUpdateDescriptorSets(Backend::getDevice(), 1, &descriptorWrite, 0, nullptr);

	deletionQueue.push_function([&]() {
		if (_descriptorPool != VK_NULL_HANDLE) {
			destroyDescriptorPool();
			_descriptorPool = VK_NULL_HANDLE;
		}
		if (descriptors.descriptorLayout != VK_NULL_HANDLE) {
			vkDestroyDescriptorSetLayout(Backend::getDevice(), descriptors.descriptorLayout, nullptr);
			descriptors.descriptorLayout = VK_NULL_HANDLE;
		}
	});
}

void DescriptorManager::addBinding(uint32_t binding, VkDescriptorType type) {
	VkDescriptorSetLayoutBinding newBind {
		.binding = binding,
		.descriptorType = type,
		.descriptorCount = 1 // change this yes
	};

	_bindings.push_back(newBind);
}

void DescriptorManager::clear() {
	_bindings.clear();
}

VkDescriptorSetLayout DescriptorManager::createSetLayout(VkShaderStageFlags shaderStages, void* pNext, VkDescriptorSetLayoutCreateFlags flags) {
	for (auto& b : _bindings) {
		b.stageFlags |= shaderStages;
	}

	VkDescriptorSetLayoutCreateInfo layoutInfo {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.flags = flags,
		.bindingCount = static_cast<uint32_t>(_bindings.size()),
		.pBindings = _bindings.data(),
	};

	VkDescriptorSetLayout set;
	VK_CHECK(vkCreateDescriptorSetLayout(Backend::getDevice(), &layoutInfo, nullptr, &set));

	return set;
}

VkDescriptorSet DescriptorManager::allocateDescriptor(VkDescriptorSetLayout layout) {
	VkDescriptorSetAllocateInfo allocInfo {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.pNext = nullptr,
		.descriptorPool = _descriptorPool,
		.descriptorSetCount = 1,
		.pSetLayouts = &layout
	};

	VkDescriptorSet ds;
	VK_CHECK(vkAllocateDescriptorSets(Backend::getDevice(), &allocInfo, &ds));

	return ds;
}

void DescriptorManager::createDescriptorPool(uint32_t maxSets, std::span<PoolSizeRatio> poolRatios) {
	std::vector<VkDescriptorPoolSize> poolSizes;
	for (PoolSizeRatio ratio : poolRatios) {
		poolSizes.push_back(VkDescriptorPoolSize{
			.type = ratio.type,
			.descriptorCount = static_cast<uint32_t>(ratio.ratio * maxSets)
		});
	}

	VkDescriptorPoolCreateInfo poolInfo {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = maxSets,
		.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
		.pPoolSizes = poolSizes.data()
	};

	VK_CHECK(vkCreateDescriptorPool(Backend::getDevice(), &poolInfo, nullptr, &_descriptorPool));
}

void DescriptorManager::clearDescriptors() {
	vkResetDescriptorPool(Backend::getDevice(), _descriptorPool, 0);
	_descriptorPool = VK_NULL_HANDLE;
}

void DescriptorManager::destroyDescriptorPool() {
	vkDestroyDescriptorPool(Backend::getDevice(), _descriptorPool, nullptr);
}

namespace DescriptorSetOverwatch {
	// allocates pools and layouts
	DescriptorManager descriptorManager;

	DescriptorsCentral meshesDescriptors{};
	DescriptorsCentral drawImageDescriptors{};

	DescriptorsCentral& getMeshesDescriptors() { return meshesDescriptors; }
	DescriptorsCentral& getDrawImageDescriptors() { return drawImageDescriptors; }
	void initDrawImageDescriptors();
	void initMeshesDescriptors();
}

// all backend needs to call
void DescriptorSetOverwatch::initAllDescriptors() {
	initDrawImageDescriptors();
	descriptorManager.clear();
	initMeshesDescriptors();
}

void DescriptorSetOverwatch::initDrawImageDescriptors() {
	drawImageDescriptors.descriptorInfo = {
		.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		.binding = 0,
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
		.imageView = Renderer::getDrawImage().imageView,
		.sampler = VK_NULL_HANDLE,
		.imageLayout = VK_IMAGE_LAYOUT_GENERAL,
		.buffer = nullptr,
		.offset = 0,
		.range = 1,
		.pNext = nullptr
	};

	descriptorManager.createDescriptors(drawImageDescriptors, Engine::getDeletionQueue());
}

// Hard coded for monkey mesh
// mesh buffers are always setup before descriptors init
void DescriptorSetOverwatch::initMeshesDescriptors() {

	meshesDescriptors.descriptorInfo = {
		.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		.binding = 0,
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
		.buffer = Scene::_sceneMeshes[2]->meshBuffers.vertexBuffer.buffer,
		.offset = 0,
		.range = VK_WHOLE_SIZE,
		.pNext = nullptr
	};

	descriptorManager.createDescriptors(meshesDescriptors, Engine::getDeletionQueue());
}
//void DescriptorManager::createDescriptorSetLayout(VkDevice _device) {
//	device = _device;
//
//	VkDescriptorSetLayoutBinding uboLayoutBinding{};
//	uboLayoutBinding.binding = 0;
//	uboLayoutBinding.descriptorCount = 1;
//	uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
//	uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; // specify which shader stage
//	uboLayoutBinding.pImmutableSamplers = nullptr; // image sampling
//
//	VkDescriptorSetLayoutBinding samplerLayoutBinding{};
//	samplerLayoutBinding.binding = 1;
//	samplerLayoutBinding.descriptorCount = 1;
//	samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
//	samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
//	samplerLayoutBinding.pImmutableSamplers = nullptr;
//
//	std::array<VkDescriptorSetLayoutBinding, 2> bindings = { uboLayoutBinding, samplerLayoutBinding };
//	VkDescriptorSetLayoutCreateInfo layoutInfo{};
//	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
//	layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
//	layoutInfo.pBindings = bindings.data();
//
//	VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout));
//}

//void DescriptorManager::createUniformBuffers() {
//	VkDeviceSize bufferSize = sizeof(UniformBufferObject);
//
//	uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
//	uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
//	uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);
//
//	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
//		BufferUtils::createBuffer(device, bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
//			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
//			uniformBuffers[i], uniformBuffersMemory[i]);
//
//		// Persistent mapping: pointer to write to the data later, lasts full application lifetime
//		vkMapMemory(device, uniformBuffersMemory[i], 0, bufferSize, 0, &uniformBuffersMapped[i]);
//	}
//}

//// could pass Texture instance?
//void DescriptorManager::createDescriptorSets(VkImageView textureImageView, VkSampler textureSampler) {
//	std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);
//	VkDescriptorSetAllocateInfo allocInfo{};
//	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
//	allocInfo.descriptorPool = descriptorPool;
//	allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
//	allocInfo.pSetLayouts = layouts.data();
//
//	descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
//	VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()));
//	// Descriptor sets automatically cleaned when pool is destroyed
//
//	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
//		VkDescriptorBufferInfo bufferInfo{};
//		bufferInfo.buffer = uniformBuffers[i];
//		bufferInfo.offset = 0;
//		bufferInfo.range = sizeof(UniformBufferObject);
//
//		VkDescriptorImageInfo imageInfo{};
//		imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
//		imageInfo.imageView = textureImageView;
//		imageInfo.sampler = textureSampler;
//
//		std::array<VkWriteDescriptorSet, 2> descriptorWrites{};
//
//		descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
//		descriptorWrites[0].dstSet = descriptorSets[i];
//		descriptorWrites[0].dstBinding = 0;
//		descriptorWrites[0].dstArrayElement = 0;
//		descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
//		descriptorWrites[0].descriptorCount = 1;
//		descriptorWrites[0].pBufferInfo = &bufferInfo;
//
//		descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
//		descriptorWrites[1].dstSet = descriptorSets[i];
//		descriptorWrites[1].dstBinding = 1;
//		descriptorWrites[1].dstArrayElement = 0;
//		descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
//		descriptorWrites[1].descriptorCount = 1;
//		descriptorWrites[1].pImageInfo = &imageInfo;
//
//		vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
//	}
//}
//
//void DescriptorManager::updateUniformBuffer(uint32_t currentImage, VkExtent2D swapchainExtent) {
//	static auto startTime = std::chrono::high_resolution_clock::now();
//
//	auto currentTime = std::chrono::high_resolution_clock::now();
//	float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();
//
//	UniformBufferObject ubo{};
//	ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.f), glm::vec3(0.0f, 1.0f, 0.0f));
//	ubo.view = glm::lookAt(glm::vec3(5.0f, 5.0f, 5.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
//	ubo.proj = glm::perspective(glm::radians(45.f), swapchainExtent.width / (float)swapchainExtent.height, 0.1f, 100.0f);
//	ubo.proj[1][1] *= -1; // Flips y coordinates
//
//	// TODO: Use push constants for this
//	memcpy(uniformBuffersMapped[currentImage], &ubo, sizeof(ubo));
//}

//void DescriptorManager::cleanupDescriptor() {
//	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
//		vkDestroyBuffer(device, uniformBuffers[i], nullptr);
//		vkFreeMemory(device, uniformBuffersMemory[i], nullptr);
//	}
//	vkDestroyDescriptorPool(device, descriptorPool, nullptr);
//
//
//	vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
//}