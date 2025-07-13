#include "pch.h"

#include "ResourceManager.h"
#include "utils/RendererUtils.h"
#include "utils/BufferUtils.h"
#include "EngineState.h"
#include "renderer/Renderer.h"
#include "core/types/Texture.h"
#include "Environment.h"
#include "common/EngineConstants.h"
#include "renderer/gpu_types/CommandBuffer.h"

namespace ResourceManager {
	ImageTable _globalImageTable;

	GPUEnvMapIndices _envMapIndices;

	// primary render image
	AllocatedImage _drawImage;
	AllocatedImage& getDrawImage() { return _drawImage; }
	AllocatedImage _depthImage;
	AllocatedImage& getDepthImage() { return _depthImage; }
	AllocatedImage _msaaImage;
	AllocatedImage& getMSAAImage() { return _msaaImage; }
	AllocatedImage _postProcessImage;
	AllocatedImage& getPostProcessImage() { return _postProcessImage; }
	ColorData toneMappingData;

	// Grabbed during physical device selection
	std::vector<VkSampleCountFlags> _availableSampleCounts;
	std::vector<VkSampleCountFlags>& getAvailableSampleCounts() { return _availableSampleCounts; }


	// Textures
	AllocatedImage _whiteImage;
	AllocatedImage& getWhiteImage() { return _whiteImage; }

	AllocatedImage _metalRoughImage;
	AllocatedImage& getMetalRoughImage() { return _metalRoughImage; }

	AllocatedImage _emissiveImage;
	AllocatedImage& getEmissiveImage() { return _emissiveImage; }

	AllocatedImage _aoImage;
	AllocatedImage& getAOImage() { return _aoImage; }
	AllocatedImage _normalImage;
	AllocatedImage& getNormalImage() { return _normalImage; }
	AllocatedImage _errorCheckerboardImage;
	AllocatedImage& getCheckboardTex() { return _errorCheckerboardImage; }

	VkSampler _defaultSamplerLinear;
	VkSampler _defaultSamplerNearest;
	VkSampler getDefaultSamplerLinear() { return _defaultSamplerLinear; }
	VkSampler getDefaultSamplerNearest() { return _defaultSamplerNearest; }


	AllocatedImage _skyboxImage;
	AllocatedImage& getSkyBoxImage() { return _skyboxImage; }

	VkSampler _skyBoxSampler;
	VkSampler& getSkyBoxSampler() { return _skyBoxSampler; }

	AllocatedImage _specularPrefilterImage;
	AllocatedImage& getSpecularPrefilterImage() { return _specularPrefilterImage; }

	VkSampler _specularPrefilterSampler;
	VkSampler& getSpecularPrefilterSampler() { return _specularPrefilterSampler; }

	AllocatedImage _irradianceImage;
	AllocatedImage& getIrradianceImage() { return _irradianceImage; }

	VkSampler _irradianceSampler;
	VkSampler& getIrradianceSampler() { return _irradianceSampler; }

	AllocatedImage _brdfLutImage;
	AllocatedImage& getBRDFImage() { return _brdfLutImage; }

	VkSampler _brdfSampler;
	VkSampler& getBRDFSampler() { return _brdfSampler; }
}


void GPUResources::init(VkDevice device) {
	allocator = VulkanUtils::createAllocator(Backend::getPhysicalDevice(), device, Backend::getInstance());
	graphicsPool = CommandBuffer::createCommandPool(device, Backend::getGraphicsQueue().familyIndex);
	transferPool = CommandBuffer::createCommandPool(device, Backend::getTransferQueue().familyIndex);
	computePool = CommandBuffer::createCommandPool(device, Backend::getComputeQueue().familyIndex);
}

void GPUResources::updateAddressTableMapped(VkCommandPool transferCommandPool, bool force) {
	std::scoped_lock lock(addressTableMutex);

	// Early out if not forced and no changes detected
	if (!force && !addressTableDirty) return;

	if (addressTableStagingBuffer.buffer == VK_NULL_HANDLE) {
		addressTableStagingBuffer = BufferUtils::createBuffer(
			sizeof(GPUAddressTable),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
			allocator
		);
		ASSERT(addressTableStagingBuffer.info.pMappedData);
	}

	// Copy latest address data into mapped buffer
	memcpy(addressTableStagingBuffer.info.pMappedData, &gpuAddresses, sizeof(GPUAddressTable));

	ASSERT(addressTableBuffer.buffer != VK_NULL_HANDLE);

	CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {
		VkBufferCopy copyRegion{};
		copyRegion.size = sizeof(GPUAddressTable);
		vkCmdCopyBuffer(cmd, addressTableStagingBuffer.buffer, addressTableBuffer.buffer, 1, &copyRegion);
		}, transferCommandPool, QueueType::Transfer);

	addressTableDirty = false;
}

void GPUResources::cleanup(VkDevice device) {
	for (auto& [name, buf] : gpuBuffers) {
		if (buf.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(buf, allocator);
	}

	if (registeredMeshes.meshIDBuffer.buffer != VK_NULL_HANDLE)
		BufferUtils::destroyAllocatedBuffer(registeredMeshes.meshIDBuffer, allocator);

	if (envMapSetUBO.buffer != VK_NULL_HANDLE)
		BufferUtils::destroyAllocatedBuffer(envMapSetUBO, allocator);

	if (addressTableStagingBuffer.buffer != VK_NULL_HANDLE)
		BufferUtils::destroyAllocatedBuffer(addressTableStagingBuffer, allocator);

	if (addressTableBuffer.buffer != VK_NULL_HANDLE)
		BufferUtils::destroyAllocatedBuffer(addressTableBuffer, allocator);

	if (graphicsPool != VK_NULL_HANDLE)
		vkDestroyCommandPool(device, graphicsPool, nullptr);

	if (transferPool != VK_NULL_HANDLE)
		vkDestroyCommandPool(device, transferPool, nullptr);

	if (computePool != VK_NULL_HANDLE)
		vkDestroyCommandPool(device, computePool, nullptr);

	if (allocator != nullptr)
		vmaDestroyAllocator(allocator);
}

void GPUResources::addGPUBufferToGlobalAddress(AddressBufferType addressBufferType, AllocatedBuffer gpuBuffer) {
	gpuBuffers[addressBufferType] = gpuBuffer;
	markAddressTableDirty();
}

uint32_t ImageTable::pushCombined(VkImageView view, VkSampler sampler) {
	std::scoped_lock lock(combinedMutex);

	ASSERT(view != VK_NULL_HANDLE && sampler != VK_NULL_HANDLE && "Null handle in pushCombined");

	ImageViewSamplerKey key = makeKey(view, sampler);

	auto it = combinedViewHashToID.find(key);
	if (it != combinedViewHashToID.end())
		return it->second;

	VkDescriptorImageInfo info{};
	info.imageView = view;
	info.sampler = sampler;
	info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	fmt::print("[ImageTable::pushCombined] New entry: view={}, sampler={}, layout=0x{:08X}\n",
		(void*)view, (void*)sampler, static_cast<uint32_t>(info.imageLayout));

	uint32_t index = static_cast<uint32_t>(combinedViews.size());
	combinedViews.push_back(info);
	combinedViewHashToID[key] = index;
	fmt::print("Index: {}\n", index);

	return index;
}

uint32_t ImageTable::pushSamplerCube(VkImageView view, VkSampler sampler) {
	std::scoped_lock lock(samplerCubeMutex);

	ASSERT(view != VK_NULL_HANDLE && sampler != VK_NULL_HANDLE && "Null handle in pushSamplerCube");

	ImageViewSamplerKey key = makeKey(view, sampler);

	auto it = samplerCubeViewHashToID.find(key);
	if (it != samplerCubeViewHashToID.end())
		return it->second;

	VkDescriptorImageInfo info{};
	info.imageView = view;
	info.sampler = sampler;
	info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	fmt::print("[ImageTable::pushSamplerCube] New entry: view={}, sampler={}, layout=0x{:08X}\n",
		(void*)view, (void*)sampler, static_cast<uint32_t>(info.imageLayout));

	uint32_t index = static_cast<uint32_t>(samplerCubeViews.size());
	samplerCubeViews.push_back(info);
	samplerCubeViewHashToID[key] = index;
	fmt::print("Index: {}\n", index);

	return index;
}

uint32_t ImageTable::pushStorage(VkImageView view) {
	std::scoped_lock lock(storageMutex);

	ASSERT(view != VK_NULL_HANDLE && "Null handle in pushStorage");

	size_t hash = std::hash<std::uintptr_t>{}(reinterpret_cast<std::uintptr_t>(view));

	auto it = storageViewHashToID.find(hash);
	if (it != storageViewHashToID.end())
		return it->second;

	VkDescriptorImageInfo info{};
	info.imageView = view;
	info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
	info.sampler = VK_NULL_HANDLE;

	fmt::print("[ImageTable::pushStorage] New entry: view={}, layout=0x{:08X}\n",
		(void*)view, static_cast<uint32_t>(info.imageLayout));

	uint32_t index = static_cast<uint32_t>(storageViews.size());
	storageViews.push_back(info);
	storageViewHashToID[hash] = index;
	fmt::print("Index: {}\n", index);

	return index;
}

void ResourceManager::initRenderImages(DeletionQueue& queue, const VmaAllocator allocator) {
	auto extent = Renderer::getDrawExtent();

	_drawImage.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
	_drawImage.imageExtent = extent;

	VkImageUsageFlags drawImageUsages{};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;

	// non sampled image
	// primary draw image color target
	RendererUtils::createRenderImage(_drawImage,
		drawImageUsages,
		VK_SAMPLE_COUNT_1_BIT,
		queue,
		allocator);

	// post process image
	_postProcessImage.imageFormat = _drawImage.imageFormat;
	_postProcessImage.imageExtent = extent;

	VkImageUsageFlags postUsages{};
	postUsages |= VK_IMAGE_USAGE_STORAGE_BIT;            // for compute shader write
	postUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;       // to copy to swapchain
	postUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;       // if needed in chain

	// compute draw image for post processing
	RendererUtils::createRenderImage(_postProcessImage,
		postUsages,
		VK_SAMPLE_COUNT_1_BIT,
		queue,
		allocator);

	VkSampleCountFlagBits sampleCount = static_cast<VkSampleCountFlagBits>(CURRENT_MSAA_LVL);

	_msaaImage.imageFormat = _drawImage.imageFormat;
	_msaaImage.imageExtent = extent;

	VkImageUsageFlags msaaImageUsages{};
	msaaImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	msaaImageUsages |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;

	// msaa color attachment to the draw image
	RendererUtils::createRenderImage(_msaaImage,
		msaaImageUsages,
		sampleCount,
		queue,
		allocator);

	// DEPTH
	_depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
	_depthImage.imageExtent = extent;

	VkImageUsageFlags depthImageUsages{};
	depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	RendererUtils::createRenderImage(_depthImage,
		depthImageUsages,
		sampleCount,
		queue,
		allocator);
}

void ResourceManager::initEnvironmentImages(DeletionQueue& queue, const VmaAllocator allocator) {
	auto maxAnisotropy = Backend::getDeviceLimits().maxSamplerAnisotropy;

	VkImageUsageFlags usage =
		VK_IMAGE_USAGE_STORAGE_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
	VkFormat environmentFormat = VK_FORMAT_R32G32B32A32_SFLOAT;

	// SKYBOX
	_skyboxImage.imageExtent = Environment::CUBEMAP_EXTENTS;
	_skyboxImage.imageFormat = environmentFormat;
	_skyboxImage.isCubeMap = true;
	_skyboxImage.mipmapped = true;

	RendererUtils::createRenderImage(_skyboxImage,
		usage,
		samples,
		queue,
		allocator);

	_skyBoxSampler = Textures::createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		0.0f, maxAnisotropy, VK_TRUE);

	_specularPrefilterImage.imageExtent = Environment::CUBEMAP_EXTENTS;
	_specularPrefilterImage.imageFormat = environmentFormat;
	_specularPrefilterImage.isCubeMap = true;
	_specularPrefilterImage.mipmapped = true;
	_specularPrefilterImage.perMipStorageViews = true;
	_specularPrefilterImage.mipLevelCount = Environment::SPECULAR_PREFILTERED_MIP_LEVELS;

	RendererUtils::createRenderImage(_specularPrefilterImage,
		usage,
		samples,
		queue,
		allocator);

	_specularPrefilterSampler = Textures::createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		static_cast<float>(_specularPrefilterImage.mipLevelCount - 1), maxAnisotropy, VK_TRUE);


	_irradianceImage.imageExtent = Environment::DIFFUSE_IRRADIANCE_BASE_EXTENTS;
	_irradianceImage.imageFormat = environmentFormat;
	_irradianceImage.isCubeMap = true;
	_irradianceImage.mipLevelCount = Environment::DIFFUSE_IRRADIANCE_MIP_LEVELS;

	RendererUtils::createRenderImage(_irradianceImage,
		usage,
		samples,
		queue,
		allocator);

	_irradianceSampler = Textures::createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		VK_LOD_CLAMP_NONE, 0.0f, VK_FALSE);


	_brdfLutImage.imageExtent = Environment::LUT_IMAGE_EXTENT;
	_brdfLutImage.imageFormat = VK_FORMAT_R32G32_SFLOAT;

	RendererUtils::createRenderImage(_brdfLutImage,
		VK_IMAGE_USAGE_STORAGE_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		samples,
		queue,
		allocator);

	_brdfSampler = Textures::createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		VK_LOD_CLAMP_NONE, 0.0f, VK_FALSE);

	auto device = Backend::getDevice();
	queue.push_function([&, device] {
		vkDestroySampler(device, _skyBoxSampler, nullptr);
		vkDestroySampler(device, _irradianceSampler, nullptr);
		vkDestroySampler(device, _specularPrefilterSampler, nullptr);
		vkDestroySampler(device, _brdfSampler, nullptr);
	});
}

// TEXTURES
void ResourceManager::initTextures(
	VkCommandPool cmdPool,
	DeletionQueue& imageQueue,
	DeletionQueue& bufferQueue,
	const VmaAllocator allocator)
{
	// reuse for now
	VkExtent3D texExtent = { 1, 1, 1 };

	VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
	VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;


	_aoImage.imageExtent = texExtent;
	_aoImage.imageFormat = VK_FORMAT_R8_UNORM;
	_aoImage.mipmapped = true;

	uint8_t aoPixel = static_cast<uint8_t>(1.0f * 255); // full ambient lighting
	RendererUtils::createTextureImage(cmdPool, (void*)&aoPixel, _aoImage, usage, samples, imageQueue, bufferQueue, allocator);

	_normalImage.imageExtent = texExtent;
	_normalImage.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
	_normalImage.mipmapped = true;

	uint32_t flatNormal = glm::packUnorm4x8(glm::vec4(0.5f, 0.5f, 1.0f, 1.0f)); // X = 128, Y = 128, Z = 255, A = 255
	RendererUtils::createTextureImage(cmdPool, (void*)&flatNormal, _normalImage, usage, samples, imageQueue, bufferQueue, allocator);


	_emissiveImage.imageExtent = texExtent;
	_emissiveImage.imageFormat = format;
	_emissiveImage.mipmapped = true;

	uint32_t blackEmissive = glm::packUnorm4x8(glm::vec4(0, 0, 0, 1)); // No emission
	RendererUtils::createTextureImage(cmdPool, (void*)&blackEmissive, _emissiveImage, usage, samples, imageQueue, bufferQueue, allocator);

	_metalRoughImage.imageExtent = texExtent;
	_metalRoughImage.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
	_metalRoughImage.mipmapped = true;

	uint8_t rgPixelData[4] = {
		static_cast<uint8_t>(0.0f * 255), // metallic?
		static_cast<uint8_t>(0.5f * 255), // roughness
		static_cast<uint8_t>(0.0f * 255), // metallic?
		static_cast<uint8_t>(1.0f * 255)
	};
	RendererUtils::createTextureImage(cmdPool, (void*)&rgPixelData, _metalRoughImage, usage, samples, imageQueue, bufferQueue, allocator);


	_whiteImage.imageExtent = texExtent;
	_whiteImage.imageFormat = format;
	_whiteImage.mipmapped = true;

	uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
	RendererUtils::createTextureImage(cmdPool, (void*)&white, _whiteImage, usage, samples, imageQueue, bufferQueue, allocator);

	//checkerboard image
	uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
	std::array<uint32_t, 16 * 16 > pixels{}; //for 16x16 checkerboard texture
	for (int x = 0; x < 16; x++) {
		for (int y = 0; y < 16; y++) {
			pixels[static_cast<size_t>(y) * 16 + x] = ((x % 2) ^ (y % 2)) ? magenta : blackEmissive;
		}
	}

	VkExtent3D checkerboardedImageExtent{ 16, 16, 1 };

	_errorCheckerboardImage.imageExtent = checkerboardedImageExtent;
	_errorCheckerboardImage.imageFormat = format;
	_errorCheckerboardImage.mipmapped = true;
	RendererUtils::createTextureImage(cmdPool, pixels.data(), _errorCheckerboardImage, usage, samples, imageQueue, bufferQueue, allocator);

	_defaultSamplerLinear = Textures::createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT,
		FLT_MAX, Backend::getDeviceLimits().maxSamplerAnisotropy, VK_TRUE);

	_defaultSamplerNearest = Textures::createSampler(VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT,
		FLT_MAX, 1.0f, VK_FALSE);

	auto device = Backend::getDevice();
	imageQueue.push_function([&, device]() {
		vkDestroySampler(device, _defaultSamplerNearest, nullptr);
		vkDestroySampler(device, _defaultSamplerLinear, nullptr);
	});
}