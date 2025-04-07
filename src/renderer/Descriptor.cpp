#include "pch.h"

#include "Descriptor.h"
#include "renderer/Renderer.h"
#include "vulkan/Backend.h"
#include "renderer/RenderScene.h"

void DescriptorManager::init(uint32_t maxSets, std::span<PoolSizeRatio> poolRatios) {
	ratios.clear();

	for (auto r : poolRatios) {
		ratios.push_back(r);
	}

	VkDescriptorPool newPool = createDescriptorPool(maxSets, poolRatios);

	setsPerPool = static_cast<uint32_t>(maxSets * 1.5);

	readyPools.push_back(newPool);
}

VkDescriptorPool DescriptorManager::getPool() {
	VkDescriptorPool newPool;
	if (readyPools.size() != 0) {
		newPool = readyPools.back();
		readyPools.pop_back();
	}
	else {
		newPool = createDescriptorPool(setsPerPool, ratios);

		setsPerPool = static_cast<uint32_t>(setsPerPool * 1.5);
		if (setsPerPool > 4092) {
			setsPerPool = 4092;
		}
	}

	return newPool;
}

VkDescriptorPool DescriptorManager::createDescriptorPool(uint32_t setCount, std::span<PoolSizeRatio> poolRatios) {
	std::vector<VkDescriptorPoolSize> poolSizes;
	for (PoolSizeRatio ratio : poolRatios) {
		poolSizes.push_back(VkDescriptorPoolSize{
			.type = ratio.type,
			.descriptorCount = static_cast<uint32_t>(ratio.ratio * setCount)
		});
	}

	VkDescriptorPoolCreateInfo poolInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
		.maxSets = setCount,
		.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
		.pPoolSizes = poolSizes.data()
	};

	VkDescriptorPool descriptorPool;
	VK_CHECK(vkCreateDescriptorPool(Backend::getDevice(), &poolInfo, nullptr, &descriptorPool));

	return descriptorPool;
}

void DescriptorManager::clearPools() {
	VkDevice device = Backend::getDevice();

	for (auto p : readyPools) {
		vkResetDescriptorPool(device, p, 0);
	}
	for (auto p : fullPools) {
		vkResetDescriptorPool(device, p, 0);
		readyPools.push_back(p);
	}

	fullPools.clear();
}

void DescriptorManager::destroyPools() {
	VkDevice device = Backend::getDevice();

	for (auto p : readyPools) {
		vkDestroyDescriptorPool(device, p, nullptr);
	}
	readyPools.clear();

	for (auto p : fullPools) {
		vkDestroyDescriptorPool(device, p, nullptr);
	}
	fullPools.clear();
}

// should always clear binding before
void DescriptorManager::addBinding(uint32_t binding, VkDescriptorType type, VkShaderStageFlags stageFlags) {
	VkDescriptorSetLayoutBinding newBind {
		.binding = binding,
		.descriptorType = type,
		.descriptorCount = 1,
		.stageFlags = stageFlags
	};

	_bindings.push_back(newBind);
}

void DescriptorManager::clearBinding() {
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

VkDescriptorSet DescriptorManager::allocateDescriptor(VkDevice device, VkDescriptorSetLayout layout, void* pNext) {

	VkDescriptorPool poolToUse = getPool();

	VkDescriptorSetAllocateInfo allocInfo {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.pNext = pNext,
		.descriptorPool = poolToUse,
		.descriptorSetCount = 1,
		.pSetLayouts = &layout
	};

	VkDescriptorSet ds;
	VkResult result = vkAllocateDescriptorSets(device, &allocInfo, &ds);

	if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
		fullPools.push_back(poolToUse);

		poolToUse = getPool();
		allocInfo.descriptorPool = poolToUse;

		VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &ds));
	}
	else {
		VK_CHECK(result);
	}

	readyPools.push_back(poolToUse);
	return ds;
}

// DESCRIPTOR WRITING
void DescriptorWriter::writeBuffer(int binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type) {
	VkDescriptorBufferInfo& info = bufferInfos.emplace_back(VkDescriptorBufferInfo {
		.buffer = buffer,
		.offset = offset,
		.range = size
	});

	VkWriteDescriptorSet write = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

	write.dstBinding = binding;
	write.dstSet = VK_NULL_HANDLE; //left empty for now until we need to write it
	write.descriptorCount = 1;
	write.descriptorType = type;
	write.pBufferInfo = &info;

	writes.push_back(write);
}

void DescriptorWriter::writeImage(int binding, VkImageView image, VkSampler sampler, VkImageLayout layout, VkDescriptorType type) {
	VkDescriptorImageInfo& info = imageInfos.emplace_back(VkDescriptorImageInfo {
		.sampler = sampler,
		.imageView = image,
		.imageLayout = layout
	});

	VkWriteDescriptorSet write = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

	write.dstBinding = binding;
	write.dstSet = VK_NULL_HANDLE; // left empty for now until we need to write it
	write.descriptorCount = 1;
	write.descriptorType = type;
	write.pImageInfo = &info;

	writes.push_back(write);
}

void DescriptorWriter::clear() {
	imageInfos.clear();
	writes.clear();
	bufferInfos.clear();
}

void DescriptorWriter::updateSet(VkDevice device, VkDescriptorSet set) {
	for (VkWriteDescriptorSet& write : writes) {
		write.dstSet = set;
	}

	vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

namespace DescriptorSetOverwatch {
	// global descriptor builder, setups layouts and pools, can allocate for them
	DescriptorManager descriptorManager;

	DescriptorsCentral drawImageDescriptors{};
	DescriptorsCentral& getDrawImageDescriptors() { return drawImageDescriptors; }
}

// all backend needs to call
void DescriptorSetOverwatch::initGlobalDescriptors() {
	//create a descriptor pool that will hold 10 sets with 1 image each
	std::vector<PoolSizeRatio> sizes =
	{
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1 },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 }
	};

	descriptorManager.init(10, sizes);

	descriptorManager.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT);
	descriptorManager.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT);

	VkDescriptorSetLayout drawImageLayout = descriptorManager.createSetLayout(VK_SHADER_STAGE_COMPUTE_BIT);

	drawImageDescriptors.descriptorSet = descriptorManager.allocateDescriptor(Backend::getDevice(), drawImageLayout);
	drawImageDescriptors.descriptorLayouts.push_back(drawImageLayout);

	descriptorManager.clearBinding();

	// meshes
	descriptorManager.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT); // for vertex buffer
	descriptorManager.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT); // for texture
	RenderScene::getGPUSceneDescriptorLayout() = descriptorManager.createSetLayout(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

	DescriptorWriter drawImageWriter;
	drawImageWriter.writeImage(0, Renderer::getPostProcessImage().imageView, VK_NULL_HANDLE,
		VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
	drawImageWriter.writeImage(1, Renderer::getDrawImage().imageView, AssetManager::getDefaultSamplerLinear(),
		VK_IMAGE_LAYOUT_GENERAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	drawImageWriter.updateSet(Backend::getDevice(), drawImageDescriptors.descriptorSet);


	Engine::getDeletionQueue().push_function([&]() {
		descriptorManager.destroyPools();

		vkDestroyDescriptorSetLayout(Backend::getDevice(), drawImageDescriptors.descriptorLayouts.back(), nullptr);
		vkDestroyDescriptorSetLayout(Backend::getDevice(), RenderScene::getGPUSceneDescriptorLayout(), nullptr);
	});
}