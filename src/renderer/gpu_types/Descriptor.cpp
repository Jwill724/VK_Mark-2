#include "pch.h"

#include "Descriptor.h"
#include "vulkan/Backend.h"
#include "common/EngineConstants.h"
#include <utils/BufferUtils.h>

namespace DescriptorSetOverwatch {
	DescriptorManager mainDescriptorManager;

	DescriptorsCentral unifiedDescriptor;
	DescriptorsCentral& getUnifiedDescriptors() { return unifiedDescriptor; }

	DescriptorsCentral frameDescriptor;
	DescriptorsCentral& getFrameDescriptors() { return frameDescriptor; }

	void initUnifiedDescriptors(DeletionQueue& dQueue);
	void initFrameDescriptors(DeletionQueue& dQueue);
	void initMainDescriptorManager(DeletionQueue& dQueue);
}

void DescriptorSetOverwatch::initMainDescriptorManager(DeletionQueue& queue) {
	std::vector<PoolSizeRatio> poolSizes {
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         static_cast<float>(MAX_FRAMES_IN_FLIGHT) },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         static_cast<float>(MAX_FRAMES_IN_FLIGHT) },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          static_cast<float>(MAX_STORAGE_IMAGES) },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_SAMPLER_CUBE_IMAGES + MAX_COMBINED_SAMPLERS_IMAGES },
	};
	mainDescriptorManager.init(MAX_FRAMES_IN_FLIGHT, poolSizes);

	queue.push_function([=]() {
		mainDescriptorManager.destroyPools();
	});
}


void DescriptorSetOverwatch::initDescriptors(DeletionQueue& queue) {
	initMainDescriptorManager(queue);
	initUnifiedDescriptors(queue);
	initFrameDescriptors(queue);
}

// Unified descriptor bindings:
// Global access constant descriptors
// [0] = GPU address table (draw ranges/material buffers)
// [1] = EnvSetUBO (Environment image indexes)
// [2] = Samplercube images (environment images)
// [3] = Storage image array (All writable images)
// [4] = Combined sampler (All static global samplers, e.g, material textures)

// All image resources — textures, render targets, compute inputs/outputs —
// are stored in these arrays. Access and interpretation are handled via the
// image LUT stored in binding [0]. This design makes all image usage agnostic,
// bindless, and scalable across the entire engine.

void DescriptorSetOverwatch::initUnifiedDescriptors(DeletionQueue& queue) {
	mainDescriptorManager.clearBinding();

	mainDescriptorManager.addBinding(ADDRESS_TABLE_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_ALL, 1);
	mainDescriptorManager.addBinding(GLOBAL_BINDING_ENV_INDEX, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL, 1);

	VkShaderStageFlags imageStageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
	mainDescriptorManager.addBinding(
		GLOBAL_BINDING_SAMPLER_CUBE,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		imageStageFlags,
		MAX_SAMPLER_CUBE_IMAGES // 100 image count
	);
	mainDescriptorManager.addBinding(
		GLOBAL_BINDING_STORAGE_IMAGE,
		VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		imageStageFlags,
		MAX_STORAGE_IMAGES // 100 image count
	);
	mainDescriptorManager.addBinding(
		GLOBAL_BINDING_COMBINED_SAMPLER,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		imageStageFlags,
		MAX_COMBINED_SAMPLERS_IMAGES // 1000 image count
	);

	VkDescriptorSetLayout layout = mainDescriptorManager.createSetLayout();

	auto device = Backend::getDevice();
	unifiedDescriptor.descriptorSet = mainDescriptorManager.allocateDescriptor(device, layout, nullptr, MAX_COMBINED_SAMPLERS_IMAGES, true);
	unifiedDescriptor.descriptorLayout = layout;

	queue.push_function([layout, device]() {
		if (layout != VK_NULL_HANDLE) {
			vkDestroyDescriptorSetLayout(device, layout, nullptr);
		}
	});
}

// Per frame descriptors for dynamic data
// Only defines layout
// [0] = Storage buffer holding addresses (instance and indirect buffers)
// [1] = Scene data UBO (camera, lighting, frame constants, etc)
void DescriptorSetOverwatch::initFrameDescriptors(DeletionQueue& queue) {
	mainDescriptorManager.clearBinding();

	mainDescriptorManager.addBinding(ADDRESS_TABLE_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_ALL, 1);
	mainDescriptorManager.addBinding(FRAME_BINDING_SCENE, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL, 1);

	VkDescriptorSetLayout layout = mainDescriptorManager.createSetLayout();
	frameDescriptor.descriptorLayout = layout;

	queue.push_function([layout]() {
		auto device = Backend::getDevice();
		if (layout != VK_NULL_HANDLE) {
			vkDestroyDescriptorSetLayout(device, layout, nullptr);
		}
	});
}

void DescriptorManager::init(uint32_t maxSets, std::span<PoolSizeRatio> poolRatios) {
	ratios.clear();

	for (auto& r : poolRatios) {
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
	for (auto& ratio : poolRatios) {
		poolSizes.push_back(VkDescriptorPoolSize{
			.type = ratio.type,
			.descriptorCount = static_cast<uint32_t>(ratio.ratio * setCount)
		});
	}

	VkDescriptorPoolCreateInfo poolInfo {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT |
		VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
		.maxSets = setCount,
		.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
		.pPoolSizes = poolSizes.data()
	};

	VkDescriptorPool descriptorPool;
	VK_CHECK(vkCreateDescriptorPool(Backend::getDevice(), &poolInfo, nullptr, &descriptorPool));

	return descriptorPool;
}

void DescriptorManager::clearPools() {
	auto device = Backend::getDevice();

	for (auto& p : readyPools) {
		vkResetDescriptorPool(device, p, 0);
	}
	for (auto& p : fullPools) {
		vkResetDescriptorPool(device, p, 0);
		readyPools.push_back(p);
	}

	fullPools.clear();
}

void DescriptorManager::destroyPools() {
	auto device = Backend::getDevice();

	for (auto& p : readyPools) {
		vkDestroyDescriptorPool(device, p, nullptr);
	}
	readyPools.clear();

	for (auto& p : fullPools) {
		vkDestroyDescriptorPool(device, p, nullptr);
	}
	fullPools.clear();
}

// should always clear binding before new set is created
void DescriptorManager::addBinding(uint32_t binding, VkDescriptorType type, VkShaderStageFlags stageFlags, uint32_t count) {
	VkDescriptorSetLayoutBinding newBind {
		.binding = binding,
		.descriptorType = type,
		.descriptorCount = count,
		.stageFlags = stageFlags
	};

	_bindings.push_back(newBind);
}

void DescriptorManager::clearBinding() {
	_bindings.clear();
}

// Last binding is the largest size for variable binding count
VkDescriptorSetLayout DescriptorManager::createSetLayout() {
	std::sort(_bindings.begin(), _bindings.end(), [](auto& a, auto& b) {
		return a.binding < b.binding;
	});

	uint32_t highestBinding = 0;
	for (const auto& b : _bindings)
		highestBinding = std::max(highestBinding, b.binding);

	std::vector<VkDescriptorBindingFlags> bindingFlags;
	for (const auto& binding : _bindings) {
		VkDescriptorBindingFlags flags = 0;

		// Always allow updating while in use
		flags |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
			VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT |
			VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

		if (binding.binding == highestBinding && binding.descriptorCount > 1) {
			flags |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
		}

		bindingFlags.push_back(flags);
	}

	ASSERT(highestBinding == _bindings.back().binding && "Variable descriptor binding must be last");

	VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
		.bindingCount = static_cast<uint32_t>(bindingFlags.size()),
		.pBindingFlags = bindingFlags.data()
	};

	VkDescriptorSetLayoutCreateInfo layoutInfo {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.pNext = &bindingFlagsInfo,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
		.bindingCount = static_cast<uint32_t>(_bindings.size()),
		.pBindings = _bindings.data()
	};

	VkDescriptorSetLayout set;
	VK_CHECK(vkCreateDescriptorSetLayout(Backend::getDevice(), &layoutInfo, nullptr, &set));

	return set;
}

VkDescriptorSet DescriptorManager::allocateDescriptor(VkDevice device, VkDescriptorSetLayout layout, void* pNext, uint32_t count, bool useVariableCount) {
	VkDescriptorPool poolToUse = getPool();

	void* finalPNext = pNext;
	VkDescriptorSetVariableDescriptorCountAllocateInfo countInfo{};
	if (useVariableCount) {
		countInfo = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO,
			.descriptorSetCount = 1,
			.pDescriptorCounts = &count
		};
		finalPNext = &countInfo;
	}

	VkDescriptorSetAllocateInfo allocInfo {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.pNext = finalPNext,
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

// TODO: Create a way to turn on debugging text easier
// DESCRIPTOR WRITING
void DescriptorWriter::writeBuffer(uint32_t binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type, VkDescriptorSet set) {
	size_t bufferIndex = bufferInfos.size();
	bufferInfos.emplace_back(VkDescriptorBufferInfo{
		.buffer = buffer,
		.offset = offset,
		.range = size
	});

	//fmt::print("Writing buffer at binding {}:\n", binding);
	//fmt::print("  Buffer Handle: {}\n", reinterpret_cast<uintptr_t>(buffer));
	//fmt::print("  Offset: {}\n", offset);
	//fmt::print("  Size: {}\n", size);
	//fmt::print("  Descriptor Type: {}\n", static_cast<int>(type));
	//fmt::print("  Descriptor Set: {}\n", reinterpret_cast<uintptr_t>(set));
	//fmt::print("  Internal Buffer Index: {}\n", bufferIndex);

	bufferWrites.push_back({
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = set,
		.dstBinding = binding,
		.descriptorCount = 1,
		.descriptorType = type,
		.pBufferInfo = nullptr,
	});

	writeBufferIndices.push_back(bufferIndex);
}

void DescriptorWriter::writeFromImageLUT(const std::vector<ImageLUTEntry>& lut, const ImageTable& table) {
	for (size_t i = 0; i < lut.size(); ++i) {
		const auto& e = lut[i];

		if (e.samplerCubeIndex != UINT32_MAX && e.samplerCubeIndex < table.samplerCubeViews.size()) {
			const auto& info = table.samplerCubeViews[e.samplerCubeIndex];
			fmt::print("[LUT {}] Pushing SamplerCube: view={}, sampler={}, layout=0x{:08X}\n",
				i, (void*)info.imageView, (void*)info.sampler, static_cast<uint32_t>(info.imageLayout));
			samplerCubeDescriptors.push_back(info);
		}
		else {
			fmt::print("[LUT {}] Skipped SamplerCube (invalid index = {})\n", i, e.samplerCubeIndex);
		}

		if (e.storageImageIndex != UINT32_MAX && e.storageImageIndex < table.storageViews.size()) {
			const auto& info = table.storageViews[e.storageImageIndex];
			fmt::print("[LUT {}] Pushing StorageImage: view={}, layout=0x{:08X}\n",
				i, (void*)info.imageView, static_cast<uint32_t>(info.imageLayout));
			storageDescriptors.push_back(info);
		}
		else {
			fmt::print("[LUT {}] Skipped StorageImage (invalid index = {})\n", i, e.storageImageIndex);
		}

		if (e.combinedImageIndex != UINT32_MAX && e.combinedImageIndex < table.combinedViews.size()) {
			const auto& info = table.combinedViews[e.combinedImageIndex];
			fmt::print("[LUT {}] Pushing CombinedImage: view={}, sampler={}, layout=0x{:08X}\n",
				i, (void*)info.imageView, (void*)info.sampler, static_cast<uint32_t>((uint32_t)info.imageLayout));
			combinedDescriptors.push_back(info);
		}
		else {
			fmt::print("[LUT {}] Skipped CombinedImage (invalid index = {})\n", i, e.combinedImageIndex);
		}
	}
}

void DescriptorWriter::writeImages(uint32_t binding, DescriptorImageType type, VkDescriptorSet set) {
	const std::vector<VkDescriptorImageInfo>* selected = nullptr;
	VkDescriptorType vkType;

	switch (type) {
	case DescriptorImageType::SamplerCube:    selected = &samplerCubeDescriptors;  vkType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; break;
	case DescriptorImageType::StorageImage:   selected = &storageDescriptors;      vkType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;          break;
	case DescriptorImageType::CombinedSampler:selected = &combinedDescriptors;     vkType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; break;
	default: ASSERT(false && "Invalid DescriptorImageType"); return;
	}
	if (!selected || selected->empty()) return;

	imageWriteGroups.push_back({
		.binding = binding,
		.type = vkType,
		.dstSet = set,
		.imageInfos = *selected
	});
}


void DescriptorWriter::clear() {
	imageWriteGroups.clear();
	bufferWrites.clear();
	writeBufferIndices.clear();
	bufferInfos.clear();
	samplerCubeDescriptors.clear();
	storageDescriptors.clear();
	combinedDescriptors.clear();
}

void DescriptorWriter::updateSet(VkDevice device, VkDescriptorSet set) {
	std::vector<VkWriteDescriptorSet> writes;

	uint32_t totalImageCount = 0;
	for (const auto& group : imageWriteGroups) {
		if (group.imageInfos.empty()) continue;

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = group.dstSet;
		write.dstBinding = group.binding;
		write.descriptorCount = static_cast<uint32_t>(group.imageInfos.size());
		write.descriptorType = group.type;
		write.pImageInfo = group.imageInfos.data();
		totalImageCount += static_cast<uint32_t>(group.imageInfos.size());

		writes.push_back(write);
	}

	if (!writes.empty()) {
		fmt::print("Total image write count: {}\n", totalImageCount);
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
	}

	if (!bufferWrites.empty()) {
		for (size_t i = 0; i < bufferWrites.size(); ++i) {
			bufferWrites[i].dstSet = set;
			bufferWrites[i].pBufferInfo = &bufferInfos[writeBufferIndices[i]];
		}
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(bufferWrites.size()), bufferWrites.data(), 0, nullptr);
	}
}