#include "pch.h"

#include "Descriptor.h"
#include "common/EngineConstants.h"
#include "utils/BufferUtils.h"
#include "engine/platform/profiler/Profiler.h"

// Align up to 4 bytes
constexpr static uint32_t Align4(uint32_t x) { return (x + 3u) & ~3u; }
// Size of inline block in bytes (multiple of 4)
constexpr uint32_t kDebugInlineBytes = Align4(sizeof(DebugToggles));

constexpr VkShaderStageFlags BASE_STAGES = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
constexpr VkShaderStageFlags IMAGE_STAGES = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

namespace DescriptorSetOverwatch {
	DescriptorManager mainDescriptorManager;

	DescriptorsCentral unifiedDescriptor;
	DescriptorsCentral& getUnifiedDescriptor() { return unifiedDescriptor; }

	DescriptorsCentral frameDescriptor;
	DescriptorsCentral& getFrameDescriptor() { return frameDescriptor; }

	DescriptorsCentral pushDescriptor;
	DescriptorsCentral& getPushDescriptor() { return pushDescriptor; }

	void initUnifiedDescriptor(const VkDevice device, DeletionQueue& dQueue);
	void initFrameDescriptor(const VkDevice device, DeletionQueue& dQueue);
	void initPushDescriptor(const VkDevice device, DeletionQueue& dQueue);
	void initMainDescriptorManager(const VkDevice device, DeletionQueue& dQueue);
}

void DescriptorSetOverwatch::initMainDescriptorManager(const VkDevice device, DeletionQueue& queue) {
	std::vector<PoolSizeRatio> poolSizes {
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         static_cast<float>(MAX_FRAMES_IN_FLIGHT) },
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         static_cast<float>(MAX_FRAMES_IN_FLIGHT) },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<float>(MAX_SAMPLER_CUBE_IMAGES + MAX_COMBINED_SAMPLERS_IMAGES) },
		{ VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK,   static_cast<float>(kDebugInlineBytes) },
	};
	mainDescriptorManager.init(device, MAX_FRAMES_IN_FLIGHT, poolSizes);

	queue.push_function([=]() {
		mainDescriptorManager.destroyPools(device);
	});
}


void DescriptorSetOverwatch::initDescriptors(const VkDevice device, DeletionQueue& queue) {
	initMainDescriptorManager(device, queue);
	initUnifiedDescriptor(device, queue);
	initFrameDescriptor(device, queue);
	initPushDescriptor(device, queue);
}

// Unified descriptor bindings:
// Global access constant descriptors
// [0] = GPU address table (draw ranges/material buffers, GPU ONLY SSBOs)
// [1] = EnvSetUBO (Environment image indexes)
// [2] = inline uniform, debug toggles and draw stats
// [3] = Samplercube images (Environment images)
// [4] = Combined sampler (Static global combined samplers, mostly materials)

// All static image resources - textures, render targets -
// are stored in these arrays. Access and interpretation are handled via the
// image LUT stored in binding [0]. This design makes all image usage agnostic,
// bindless, and scalable across the entire engine.

void DescriptorSetOverwatch::initUnifiedDescriptor(const VkDevice device, DeletionQueue& queue) {
	mainDescriptorManager.clearBinding();

	mainDescriptorManager.addBinding(ADDRESS_TABLE_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BASE_STAGES, 1);
	mainDescriptorManager.addBinding(GLOBAL_BINDING_ENV_INDEX, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BASE_STAGES, 1);
	mainDescriptorManager.addBinding(GLOBAL_BINDING_DEBUG_INLINE, VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK, BASE_STAGES, kDebugInlineBytes);

	mainDescriptorManager.addBinding(
		GLOBAL_BINDING_SAMPLER_CUBE,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		IMAGE_STAGES,
		MAX_SAMPLER_CUBE_IMAGES // 100 image count
	);
	mainDescriptorManager.addBinding(
		GLOBAL_BINDING_COMBINED_SAMPLER,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		IMAGE_STAGES,
		MAX_COMBINED_SAMPLERS_IMAGES // 10000 image count
	);

	VkDescriptorSetLayout layout = mainDescriptorManager.createSetLayout(device);

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
// [2] = Light frustum cascades, splits and shadow bias, etc
// [3] = Clustered shading data
void DescriptorSetOverwatch::initFrameDescriptor(const VkDevice device, DeletionQueue& queue) {
	mainDescriptorManager.clearBinding();

	mainDescriptorManager.addBinding(ADDRESS_TABLE_BINDING, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BASE_STAGES, 1);
	mainDescriptorManager.addBinding(FRAME_BINDING_SCENE, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BASE_STAGES, 1);
	mainDescriptorManager.addBinding(FRAME_BINDING_CSM, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BASE_STAGES, 1);
	mainDescriptorManager.addBinding(FRAME_BINDING_CLUSTERED, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BASE_STAGES, 1);

	VkDescriptorSetLayout layout = mainDescriptorManager.createSetLayout(device);
	frameDescriptor.descriptorLayout = layout;

	queue.push_function([layout, device]() {
		if (layout != VK_NULL_HANDLE) {
			vkDestroyDescriptorSetLayout(device, layout, nullptr);
		}
	});
}

// Push descriptor bindings will be filled up over time
void DescriptorSetOverwatch::initPushDescriptor(const VkDevice device, DeletionQueue& queue) {
	mainDescriptorManager.clearBinding();

	// Readable inputs
	for (uint32_t i = PUSH_BINDING_READ_1; i <= PUSH_BINDING_READ_7; i++) {
		mainDescriptorManager.addBinding(
		i,
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		IMAGE_STAGES,
		1);
	}

	// Writable outputs
	for (uint32_t i = PUSH_BINDING_WRITE_1; i <= PUSH_BINDING_WRITE_5; i++) {
		mainDescriptorManager.addBinding(
		i,
		VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		IMAGE_STAGES,
		1);
	}

	VkDescriptorSetLayout layout = mainDescriptorManager.createPushSetLayout(device);

	pushDescriptor.descriptorLayout = layout;

	queue.push_function([layout, device]() {
		if (layout != VK_NULL_HANDLE) {
			vkDestroyDescriptorSetLayout(device, layout, nullptr);
		}
	});
}

void DescriptorManager::init(const VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios) {
	ratios.clear();

	for (auto& r : poolRatios) {
		ratios.push_back(r);
	}

	VkDescriptorPool newPool = createDescriptorPool(device, maxSets, poolRatios);

	setsPerPool = static_cast<uint32_t>(maxSets * 1.5);

	readyPools.push_back(newPool);
}

VkDescriptorPool DescriptorManager::getPool(const VkDevice device) {
	VkDescriptorPool newPool;
	if (readyPools.size() != 0) {
		newPool = readyPools.back();
		readyPools.pop_back();
	}
	else {
		newPool = createDescriptorPool(device, setsPerPool, ratios);

		setsPerPool = static_cast<uint32_t>(setsPerPool * 1.5);
		if (setsPerPool > 4092) {
			setsPerPool = 4092;
		}
	}

	return newPool;
}

VkDescriptorPool DescriptorManager::createDescriptorPool(const VkDevice device, uint32_t setCount, std::span<PoolSizeRatio> poolRatios) {
	std::vector<VkDescriptorPoolSize> poolSizes;
	for (auto& ratio : poolRatios) {
		poolSizes.push_back(VkDescriptorPoolSize{
			.type = ratio.type,
			.descriptorCount = static_cast<uint32_t>(ratio.ratio * setCount)
		});
	}

	const uint32_t inlineBytesTotal = kDebugInlineBytes * setCount;
	if (inlineBytesTotal) {
		poolSizes.push_back(VkDescriptorPoolSize{
			.type = VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK,
			.descriptorCount = inlineBytesTotal
		});
	}

	VkDescriptorPoolInlineUniformBlockCreateInfo inlineInfo{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_INLINE_UNIFORM_BLOCK_CREATE_INFO,
		.pNext = nullptr,
		.maxInlineUniformBlockBindings = setCount // one inline binding per set
	};

	VkDescriptorPoolCreateInfo poolInfo {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.pNext = &inlineInfo,
		.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT |
		VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
		.maxSets = setCount,
		.poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
		.pPoolSizes = poolSizes.data()
	};

	VkDescriptorPool descriptorPool;
	VK_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool));

	return descriptorPool;
}

void DescriptorManager::clearPools(const VkDevice device) {
	for (auto& p : readyPools) {
		vkResetDescriptorPool(device, p, 0);
	}
	for (auto& p : fullPools) {
		vkResetDescriptorPool(device, p, 0);
		readyPools.push_back(p);
	}

	fullPools.clear();
}

void DescriptorManager::destroyPools(const VkDevice device) {
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
VkDescriptorSetLayout DescriptorManager::createSetLayout(const VkDevice device) {
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
	VK_CHECK(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &set));

	return set;
}

VkDescriptorSet DescriptorManager::allocateDescriptor(
	const VkDevice device,
	VkDescriptorSetLayout layout,
	void* pNext,
	uint32_t count,
	bool useVariableCount)
{
	VkDescriptorPool poolToUse = getPool(device);

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
		poolToUse = getPool(device);
		allocInfo.descriptorPool = poolToUse;
		VK_CHECK(vkAllocateDescriptorSets(device, &allocInfo, &ds));
	}
	else {
		VK_CHECK(result);
	}

	readyPools.push_back(poolToUse);
	return ds;
}

// Push descriptors
VkDescriptorSetLayout DescriptorManager::createPushSetLayout(const VkDevice device) {
	std::sort(_bindings.begin(), _bindings.end(), [](auto& a, auto& b) { return a.binding < b.binding; });

	VkDescriptorSetLayoutCreateInfo info{
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
		.bindingCount = static_cast<uint32_t>(_bindings.size()),
		.pBindings = _bindings.data()
	};

	VkDescriptorSetLayout set{};
	VK_CHECK(vkCreateDescriptorSetLayout(device, &info, nullptr, &set));
	return set;
}

// === DESCRIPTOR WRITING ===
void DescriptorWriter::writePushImage(
	uint32_t binding,
	AllocatedImage& image,
	VkSampler sampler,
	VkImageLayout overrideLayout,
	uint32_t storageViewIndex)
{
	enablePushDescriptor = true;

	VkDescriptorType type = (sampler != VK_NULL_HANDLE) ?
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

	VkImageView view = image.imageView;
	if (storageViewIndex != UINT32_MAX && (storageViewIndex >= 0u && storageViewIndex < image.mipLevelCount)) {
		view = image.storageViews[static_cast<size_t>(storageViewIndex)];
	}
	VkImageLayout layout = image.currentLayout;
	if (overrideLayout != VK_IMAGE_LAYOUT_MAX_ENUM) {
		layout = overrideLayout;
	}

	VkDescriptorImageInfo imageInfo{ sampler, view, layout };
	imageWriteGroups.push_back({
		.binding = binding,
		.type = type,
		.dstSet = VK_NULL_HANDLE,
		.imageInfo = imageInfo
	});
}


void DescriptorWriter::updatePushSet(
	VkCommandBuffer cmd,
	VkPipelineBindPoint bindPoint,
	VkPipelineLayout pipelineLayout)
{
	std::vector<VkWriteDescriptorSet> writes;
	writes.reserve(bufferWrites.size() + imageWriteGroups.size());

	if (!bufferWrites.empty()) {
		for (size_t i = 0; i < bufferWrites.size(); ++i) {
			bufferWrites[i].dstSet = VK_NULL_HANDLE;
			bufferWrites[i].dstArrayElement = 0;
			bufferWrites[i].pBufferInfo = &bufferInfos[writeBufferIndices[i]];
			writes.push_back(bufferWrites[i]);
		}
	}

	if (!imageWriteGroups.empty()) {
		for (const auto& group : imageWriteGroups) {
			VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
			write.dstSet = VK_NULL_HANDLE;
			write.dstArrayElement = 0;
			write.dstBinding = group.binding;
			write.descriptorCount = 1u;
			write.descriptorType = group.type;
			write.pImageInfo = &group.imageInfo;
			writes.push_back(write);
		}
	}

	if (!writes.empty()) {
		vkCmdPushDescriptorSet(
			cmd,
			bindPoint,
			pipelineLayout,
			PUSH_SET, // Hard coded set 2
			static_cast<uint32_t>(writes.size()),
			writes.data());

		clear();
		enablePushDescriptor = false;
	}
}

// Normal descriptor writing
void DescriptorWriter::writeBuffer(
	uint32_t binding,
	VkBuffer buffer,
	size_t size,
	size_t offset,
	VkDescriptorType type,
	VkDescriptorSet set)
{
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
		.descriptorCount = 1u,
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
			if (ENABLE_DEBUG_LOGS) {
				fmt::print("[LUT {}] Pushing SamplerCube: view={}, sampler={}, layout=0x{:08X}\n",
					i, (void*)info.imageView, (void*)info.sampler, static_cast<uint32_t>(info.imageLayout));
			}
			samplerCubeDescriptors.push_back(info);
		}
		//else {
		//	if (ENABLE_DEBUG_LOGS) fmt::print("[LUT {}] Skipped SamplerCube (invalid index = {})\n", i, e.samplerCubeIndex);
		//}

		if (e.combinedImageIndex != UINT32_MAX && e.combinedImageIndex < table.combinedViews.size()) {
			const auto& info = table.combinedViews[e.combinedImageIndex];
			if (ENABLE_DEBUG_LOGS) {
				fmt::print("[LUT {}] Pushing CombinedImage: view={}, sampler={}, layout=0x{:08X}\n",
					i, (void*)info.imageView, (void*)info.sampler, static_cast<uint32_t>(info.imageLayout));
			}
			combinedDescriptors.push_back(info);
		}
		//else {
		//	if (ENABLE_DEBUG_LOGS) fmt::print("[LUT {}] Skipped CombinedImage (invalid index = {})\n", i, e.combinedImageIndex);
		//}
	}
}

void DescriptorWriter::writeImages(uint32_t binding, DescriptorImageType type, VkDescriptorSet set) {
	const std::vector<VkDescriptorImageInfo>* selected = nullptr;
	VkDescriptorType vkType;

	switch (type) {
	case DescriptorImageType::SamplerCube:    selected = &samplerCubeDescriptors;  vkType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; break;
	case DescriptorImageType::CombinedSampler:selected = &combinedDescriptors;     vkType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; break;
	default: ASSERT(false && "Invalid DescriptorImageType"); return;
	}
	if (!selected || selected->empty()) return;

	imageWriteGroups.push_back({
		.binding = binding,
		.type = vkType,
		.dstSet = set,
		.v_imageInfos = *selected
	});
}

void DescriptorWriter::clear() {
	imageWriteGroups.clear();
	bufferWrites.clear();
	writeBufferIndices.clear();
	bufferInfos.clear();
	samplerCubeDescriptors.clear();
	combinedDescriptors.clear();
	shouldClearWrites = false;
	enablePushDescriptor = false;
}

void DescriptorWriter::writeInlineUniform(
	uint32_t binding,
	const void* data,
	uint32_t size,
	VkDevice device,
	VkDescriptorSet set)
{
	VkWriteDescriptorSetInlineUniformBlock inlineBlock {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK,
		.pNext = nullptr,
		.dataSize = size,
		.pData = data
	};

	VkWriteDescriptorSet write {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext = &inlineBlock,
		.dstSet = set,
		.dstBinding = binding,
		.descriptorCount = size,
		.descriptorType = VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK
	};

	vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void DescriptorWriter::updateSet(VkDevice device, VkDescriptorSet set) {
	std::vector<VkWriteDescriptorSet> writes;

	uint32_t totalImageCount = 0;
	if (!imageWriteGroups.empty()) {
		for (const auto& group : imageWriteGroups) {
			VkWriteDescriptorSet write{};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = group.dstSet;
			write.dstBinding = group.binding;
			write.descriptorCount = static_cast<uint32_t>(group.v_imageInfos.size());
			write.descriptorType = group.type;
			write.pImageInfo = group.v_imageInfos.data();
			totalImageCount += static_cast<uint32_t>(group.v_imageInfos.size());

			writes.push_back(write);
		}
	}

	if (!writes.empty()) {
		if (ENABLE_DEBUG_LOGS) fmt::print("Total image write count: {}\n\n", totalImageCount);

		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

		shouldClearWrites = true;
	}

	if (!bufferWrites.empty()) {
		for (size_t i = 0; i < bufferWrites.size(); ++i) {
			bufferWrites[i].dstSet = set;
			bufferWrites[i].pBufferInfo = &bufferInfos[writeBufferIndices[i]];
		}
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(bufferWrites.size()), bufferWrites.data(), 0, nullptr);

		shouldClearWrites = true;
	}

	if (shouldClearWrites) clear();
}
