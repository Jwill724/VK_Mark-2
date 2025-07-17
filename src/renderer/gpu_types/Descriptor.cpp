#include "pch.h"

#include "Descriptor.h"
#include "vulkan/Backend.h"
#include "common/EngineConstants.h"

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

	std::vector<PoolSizeRatio> poolSizes = {
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         static_cast<float>(1) },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         static_cast<float>(1) },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<float>(MAX_SAMPLER_CUBE_IMAGES) },
		{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          static_cast<float>(MAX_STORAGE_IMAGES) },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<float>(MAX_COMBINED_SAMPLERS_IMAGES) },
	};
	mainDescriptorManager.init(MAX_FRAMES_IN_FLIGHT * 2, poolSizes);

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
// [1] = UBO (Environment image set indexes)
// [2] = Samplercube images (environment images)
// [3] = Storage image array (All writable images)
// [4] = Combined sampler (All sampled images. materials, and texture images etc)

// All image resources — textures, render targets, compute inputs/outputs —
// are stored in these arrays. Access and interpretation are handled via the
// image LUT stored in binding [0]. This design makes all image usage agnostic,
// bindless, and scalable across the entire engine.

void DescriptorSetOverwatch::initUnifiedDescriptors(DeletionQueue& queue) {
	mainDescriptorManager.clearBinding();

	mainDescriptorManager.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_ALL, 1);
	mainDescriptorManager.addBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL, 1); // Global image indexes

	VkShaderStageFlags imageStageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
	mainDescriptorManager.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, imageStageFlags, MAX_SAMPLER_CUBE_IMAGES);
	mainDescriptorManager.addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, imageStageFlags, MAX_STORAGE_IMAGES);
	mainDescriptorManager.addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, imageStageFlags, MAX_COMBINED_SAMPLERS_IMAGES);

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

	mainDescriptorManager.addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_ALL, 1);
	mainDescriptorManager.addBinding(1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL, 1);

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

	VkDescriptorPoolCreateInfo poolInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT | VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
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
		flags |= VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

		if (binding.descriptorCount > 1) {
			flags |= VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
		}

		if (binding.binding == highestBinding && binding.descriptorCount > 1) {
			flags |= VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT;
		}

		bindingFlags.push_back(flags);
	}

	VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
		.bindingCount = static_cast<uint32_t>(bindingFlags.size()),
		.pBindingFlags = bindingFlags.data()
	};

	VkDescriptorSetLayoutCreateInfo layoutInfo{
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

	VkDescriptorSetAllocateInfo allocInfo{
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
void DescriptorWriter::writeBuffer(int binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type, VkDescriptorSet set) {
	size_t bufferIndex = bufferInfos.size();
	bufferInfos.emplace_back(VkDescriptorBufferInfo{
		.buffer = buffer,
		.offset = offset,
		.range = size
	});

	fmt::print("Writing buffer at binding {}:\n", binding);
	fmt::print("  Buffer Handle: {}\n", reinterpret_cast<uintptr_t>(buffer));
	fmt::print("  Offset: {}\n", offset);
	fmt::print("  Size: {}\n", size);
	fmt::print("  Descriptor Type: {}\n", static_cast<int>(type));
	fmt::print("  Descriptor Set: {}\n", reinterpret_cast<uintptr_t>(set));
	fmt::print("  Internal Buffer Index: {}\n", bufferIndex);

	bufferWrites.push_back({
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = set,
		.dstBinding = static_cast<uint32_t>(binding),
		.descriptorCount = 1,
		.descriptorType = type,
		.pBufferInfo = nullptr,
	});

	writeBufferIndices.push_back(bufferIndex);
}

void DescriptorWriter::writeFromImageLUT(const std::vector<ImageLUTEntry>& lut, const ImageTable& table, VkDescriptorSet descriptorSet) {
	std::vector<VkDescriptorImageInfo> samplerCubeDescriptors;
	std::vector<VkDescriptorImageInfo> storageDescriptors;
	std::vector<VkDescriptorImageInfo> combinedDescriptors;

	//fmt::print("[DescriptorWriter] Descriptor set {}\n", static_cast<void*>(descriptorSet));

	for (size_t i = 0; i < lut.size(); ++i) {
		const auto& entry = lut[i];

		// Sampler Cube
		if (entry.samplerCubeIndex != UINT32_MAX && entry.samplerCubeIndex < table.samplerCubeViews.size()) {
			const auto& info = table.samplerCubeViews[entry.samplerCubeIndex];
			//fmt::print("[LUT {}] Pushing SamplerCube: view={}, sampler={}, layout=0x{:08X}\n",
			//	i, (void*)info.imageView, (void*)info.sampler, static_cast<uint32_t>(info.imageLayout));
			samplerCubeDescriptors.push_back(info);
		}
		//else {
		//	fmt::print("[LUT {}] Skipped SamplerCube (invalid index = {})\n", i, entry.samplerCubeIndex);
		//}

		// Storage Image
		if (entry.storageImageIndex != UINT32_MAX && entry.storageImageIndex < table.storageViews.size()) {
			const auto& info = table.storageViews[entry.storageImageIndex];
			//fmt::print("[LUT {}] Pushing StorageImage: view={}, layout=0x{:08X}\n",
			//	i, (void*)info.imageView, static_cast<uint32_t>(info.imageLayout));
			storageDescriptors.push_back(info);
		}
		//else {
		//	fmt::print("[LUT {}] Skipped StorageImage (invalid index = {})\n", i, entry.storageImageIndex);
		//}

		// Combined Image
		if (entry.combinedImageIndex != UINT32_MAX && entry.combinedImageIndex < table.combinedViews.size()) {
			const auto& info = table.combinedViews[entry.combinedImageIndex];
			//fmt::print("[LUT {}] Pushing CombinedImage: view={}, sampler={}, layout=0x{:08X}\n",
			//	i, (void*)info.imageView, (void*)info.sampler, static_cast<uint32_t>(info.imageLayout));
			combinedDescriptors.push_back(info);
		}
		//else {
		//	fmt::print("[LUT {}] Skipped CombinedImage (invalid index = {})\n", i, entry.combinedImageIndex);
		//}
	}

	writeImages(2, samplerCubeDescriptors, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, descriptorSet);
	writeImages(3, storageDescriptors, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, descriptorSet);
	writeImages(4, combinedDescriptors, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, descriptorSet);
}

void DescriptorWriter::writeImages(int binding, const std::vector<VkDescriptorImageInfo>& images, VkDescriptorType type, VkDescriptorSet set) {
	if (images.empty()) {
		fmt::print("Skipped write at binding {}: no images to write\n", binding);
		return;
	}

	//fmt::print("\n[DescriptorWriter] BEGIN writeImages()\n");
	//fmt::print("[DescriptorWriter] Writing {} descriptors to set {} at binding {} (type = {})\n",
	//	images.size(), (void*)set, binding, static_cast<int>(type));
	//fmt::print("[DescriptorWriter] imageInfos capacity BEFORE insert: {}, size BEFORE insert: {}, inserting: {}\n",
	//	imageInfos.capacity(), imageInfos.size(), images.size());

	size_t startIndex = imageInfos.size();
	imageInfos.insert(imageInfos.end(), images.begin(), images.end());

	//fmt::print("[DescriptorWriter] imageInfos capacity AFTER insert: {}, size AFTER insert: {}\n", imageInfos.capacity(), imageInfos.size());

	for (size_t i = 0; i < images.size(); ++i) {
		const auto& info = images[i];

		//fmt::print("[{}] Binding {} - ", static_cast<uint32_t>(i), binding);

		if (info.imageView != VK_NULL_HANDLE) {
			//fmt::print("imageView = {}, sampler = {}, layout = 0x{:08X}\n",
			//	(void*)info.imageView, (void*)info.sampler, static_cast<uint32_t>(info.imageLayout));

			ASSERT(info.imageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ||
				info.imageLayout == VK_IMAGE_LAYOUT_GENERAL ||
				info.imageLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL ||
				info.imageLayout == VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_STENCIL_ATTACHMENT_OPTIMAL ||
				info.imageLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_STENCIL_READ_ONLY_OPTIMAL);
		}
		//else {
		//	fmt::print("imageView = VK_NULL_HANDLE (skipped layout validation)\n");
		//}
	}

	//const void* ptr = static_cast<const void*>(&imageInfos[startIndex]);
	//fmt::print("[DescriptorWriter] Write descriptor startIndex = {}, pImageInfo = {}\n", startIndex, ptr);

	// Deferred image writing into set updating
	pendingImageWrites.push_back({
		.binding = static_cast<uint32_t>(binding),
		.type = type,
		.startIndex = startIndex,
		.count = images.size(),
		.dstSet = set
	});
}

void DescriptorWriter::clear() {
	imageInfos.clear();
	imageWrites.clear();
	pendingImageWrites.clear();
	bufferWrites.clear();
	writeBufferIndices.clear();
	bufferInfos.clear();
}

void DescriptorWriter::updateSet(VkDevice device, VkDescriptorSet set) {
	if (!pendingImageWrites.empty()) {
		//fmt::print("Size of total image writes: {}\n", pendingImageWrites.size());
		for (const auto& pw : pendingImageWrites) {
			ASSERT(pw.startIndex + pw.count <= imageInfos.size());
			imageWrites.push_back({
				.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet = pw.dstSet,
				.dstBinding = pw.binding,
				.descriptorCount = static_cast<uint32_t>(pw.count),
				.descriptorType = pw.type,
				.pImageInfo = &imageInfos[pw.startIndex]
			});
		}
		if (!imageWrites.empty()) {
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(imageWrites.size()), imageWrites.data(), 0, nullptr);
		}
	}

	if (!bufferWrites.empty()) {
		for (size_t i = 0; i < bufferWrites.size(); ++i) {
			bufferWrites[i].dstSet = set;
			bufferWrites[i].pBufferInfo = &bufferInfos[writeBufferIndices[i]];
		}
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(bufferWrites.size()), bufferWrites.data(), 0, nullptr);
	}
}