#include "pch.h"

#include "ResourceManager.h"
#include "utils/BufferUtils.h"
#include "utils/VulkanUtils.h"
#include "renderer/Renderer.h"
#include "Environment.h"

namespace ResourceManager {
	ImageTableManager _globalImageManager;

	GPUEnvMapIndexArray _envMapIdxArray;

	// primary render image
	AllocatedImage _drawImage;
	AllocatedImage& getDrawImage() { return _drawImage; }
	AllocatedImage _depthImage;
	AllocatedImage& getDepthImage() { return _depthImage; }
	AllocatedImage _msaaImage;
	AllocatedImage& getMSAAImage() { return _msaaImage; }
	AllocatedImage _toneMappingImage;
	AllocatedImage& getToneMappingImage() { return _toneMappingImage; }
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
	}, transferCommandPool, QueueType::Transfer, Backend::getDevice());

	addressTableDirty = false;
}

void GPUResources::cleanup(VkDevice device) {
	for (auto& [name, buf] : gpuBuffers) {
		if (buf.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(buf, allocator);
	}

	if (registeredMeshes.meshIDBuffer.buffer != VK_NULL_HANDLE)
		BufferUtils::destroyAllocatedBuffer(registeredMeshes.meshIDBuffer, allocator);

	if (envMapIndexBuffer.buffer != VK_NULL_HANDLE)
		BufferUtils::destroyAllocatedBuffer(envMapIndexBuffer, allocator);

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

void ResourceManager::initRenderImages(
	const VkDevice device,
	DeletionQueue& queue,
	const VmaAllocator allocator,
	const VkExtent3D drawExtent)
{
	//_drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	_drawImage.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
	_drawImage.imageExtent = drawExtent;

	VkImageUsageFlags drawImageUsages{};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;

	// non sampled image
	// primary draw image color target
	ImageUtils::createRenderImage(
		device,
		_drawImage,
		drawImageUsages,
		VK_SAMPLE_COUNT_1_BIT,
		queue,
		allocator);

	// tone mapping post process image
	_toneMappingImage.imageFormat = _drawImage.imageFormat;
	_toneMappingImage.imageExtent = drawExtent;

	VkImageUsageFlags toneMapUsages{};
	toneMapUsages |= VK_IMAGE_USAGE_STORAGE_BIT;            // for compute shader write
	toneMapUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;       // to copy to swapchain
	toneMapUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;       // if needed in chain

	ImageUtils::createRenderImage(
		device,
		_toneMappingImage,
		toneMapUsages,
		VK_SAMPLE_COUNT_1_BIT,
		queue,
		allocator);

	VkSampleCountFlagBits sampleCount = !MSAA_ENABLED ? VK_SAMPLE_COUNT_1_BIT : static_cast<VkSampleCountFlagBits>(CURRENT_MSAA_LVL);

	_msaaImage.imageFormat = _drawImage.imageFormat;
	_msaaImage.imageExtent = drawExtent;

	VkImageUsageFlags msaaImageUsages{};
	msaaImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	msaaImageUsages |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;

	// msaa color attachment to the draw image
	ImageUtils::createRenderImage(
		device,
		_msaaImage,
		msaaImageUsages,
		sampleCount,
		queue,
		allocator);

	// DEPTH
	_depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
	_depthImage.imageExtent = drawExtent;

	VkImageUsageFlags depthImageUsages{};
	depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	ImageUtils::createRenderImage(
		device,
		_depthImage,
		depthImageUsages,
		sampleCount,
		queue,
		allocator);
}

void ResourceManager::initEnvironmentImages(
	const VkDevice device,
	DeletionQueue& queue,
	const VmaAllocator allocator)
{
	auto maxAnisotropy = Backend::getDeviceLimits().maxSamplerAnisotropy;

	VkImageUsageFlags usage =
		VK_IMAGE_USAGE_STORAGE_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
		VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
	VkFormat environmentFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

	// SKYBOX
	_skyboxImage.imageExtent = Environment::CUBEMAP_EXTENTS;
	_skyboxImage.imageFormat = environmentFormat;
	_skyboxImage.isCubeMap = true;
	_skyboxImage.mipmapped = true;

	ImageUtils::createRenderImage(
		device,
		_skyboxImage,
		usage,
		samples,
		queue,
		allocator);

	_skyBoxSampler = ImageUtils::createSampler(
		device,
		VK_FILTER_LINEAR,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		0.0f,
		maxAnisotropy,
		VK_TRUE);

	_specularPrefilterImage.imageExtent = Environment::CUBEMAP_EXTENTS;
	_specularPrefilterImage.imageFormat = environmentFormat;
	_specularPrefilterImage.isCubeMap = true;
	_specularPrefilterImage.mipmapped = true;
	_specularPrefilterImage.perMipStorageViews = true;
	_specularPrefilterImage.mipLevelCount = Environment::SPECULAR_PREFILTERED_MIP_LEVELS;

	ImageUtils::createRenderImage(
		device,
		_specularPrefilterImage,
		usage,
		samples,
		queue,
		allocator);

	_specularPrefilterSampler = ImageUtils::createSampler(
		device,
		VK_FILTER_LINEAR,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		static_cast<float>(_specularPrefilterImage.mipLevelCount - 1),
		maxAnisotropy,
		VK_TRUE);


	_irradianceImage.imageExtent = Environment::DIFFUSE_IRRADIANCE_BASE_EXTENTS;
	_irradianceImage.imageFormat = environmentFormat;
	_irradianceImage.isCubeMap = true;

	ImageUtils::createRenderImage(
		device,
		_irradianceImage,
		usage,
		samples,
		queue,
		allocator);

	_irradianceSampler = ImageUtils::createSampler(
		device,
		VK_FILTER_LINEAR,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		VK_LOD_CLAMP_NONE,
		0.0f,
		VK_FALSE);


	_brdfLutImage.imageExtent = Environment::LUT_IMAGE_EXTENT;
	_brdfLutImage.imageFormat = VK_FORMAT_R16G16_SFLOAT;

	ImageUtils::createRenderImage(
		device,
		_brdfLutImage,
		VK_IMAGE_USAGE_STORAGE_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT |
		VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
		samples,
		queue,
		allocator);

	_brdfSampler = ImageUtils::createSampler(
		device,
		VK_FILTER_LINEAR,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		VK_LOD_CLAMP_NONE,
		0.0f,
		VK_FALSE);

	// sampler deletion, should just place the queue into the creation function
	queue.push_function([&, device] {
		vkDestroySampler(device, _skyBoxSampler, nullptr);
		vkDestroySampler(device, _irradianceSampler, nullptr);
		vkDestroySampler(device, _specularPrefilterSampler, nullptr);
		vkDestroySampler(device, _brdfSampler, nullptr);
	});
}

void ResourceManager::initTextures(
	const VkDevice device,
	VkCommandPool cmdPool,
	DeletionQueue& imageQueue,
	DeletionQueue& bufferQueue,
	const VmaAllocator allocator)
{
	// reuse for now
	VkExtent3D texExtent { 1, 1, 1 };

	VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
	VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;


	_aoImage.imageExtent = texExtent;
	_aoImage.imageFormat = VK_FORMAT_R8_UNORM;
	_aoImage.mipmapped = true;

	uint8_t aoPixel = static_cast<uint8_t>(1.0f * 255); // full ambient lighting
	ImageUtils::createTextureImage(
		device,
		cmdPool,
		(void*)&aoPixel,
		_aoImage,
		usage,
		samples,
		imageQueue,
		bufferQueue,
		allocator);

	_normalImage.imageExtent = texExtent;
	_normalImage.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
	_normalImage.mipmapped = true;

	uint32_t flatNormal = glm::packUnorm4x8(glm::vec4(0.5f, 0.5f, 1.0f, 1.0f)); // X = 128, Y = 128, Z = 255, A = 255
	ImageUtils::createTextureImage(
		device,
		cmdPool,
		(void*)&flatNormal,
		_normalImage,
		usage,
		samples,
		imageQueue,
		bufferQueue,
		allocator);


	_emissiveImage.imageExtent = texExtent;
	_emissiveImage.imageFormat = format;
	_emissiveImage.mipmapped = true;

	uint32_t blackEmissive = glm::packUnorm4x8(glm::vec4(0, 0, 0, 1)); // No emission
	ImageUtils::createTextureImage(
		device,
		cmdPool,
		(void*)&blackEmissive,
		_emissiveImage,
		usage,
		samples,
		imageQueue,
		bufferQueue,
		allocator);

	_metalRoughImage.imageExtent = texExtent;
	_metalRoughImage.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
	_metalRoughImage.mipmapped = true;

	uint8_t mrPixelData[4] {
		static_cast<uint8_t>(0.0f * 255), // metallic?
		static_cast<uint8_t>(0.5f * 255), // roughness
		static_cast<uint8_t>(0.0f * 255), // metallic?
		static_cast<uint8_t>(1.0f * 255)
	};
	ImageUtils::createTextureImage(
		device,
		cmdPool,
		(void*)&mrPixelData,
		_metalRoughImage,
		usage,
		samples,
		imageQueue,
		bufferQueue,
		allocator);


	_whiteImage.imageExtent = texExtent;
	_whiteImage.imageFormat = format;
	_whiteImage.mipmapped = true;

	uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
	ImageUtils::createTextureImage(
		device,
		cmdPool,
		(void*)&white,
		_whiteImage,
		usage,
		samples,
		imageQueue,
		bufferQueue,
		allocator);

	// checkerboard image
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
	ImageUtils::createTextureImage(
		device,
		cmdPool,
		pixels.data(),
		_errorCheckerboardImage,
		usage,
		samples,
		imageQueue,
		bufferQueue,
		allocator);

	_defaultSamplerLinear = ImageUtils::createSampler(
		device,
		VK_FILTER_LINEAR,
		VK_SAMPLER_ADDRESS_MODE_REPEAT,
		FLT_MAX,
		Backend::getDeviceLimits().maxSamplerAnisotropy,
		VK_TRUE);

	_defaultSamplerNearest = ImageUtils::createSampler(
		device,
		VK_FILTER_NEAREST,
		VK_SAMPLER_ADDRESS_MODE_REPEAT,
		FLT_MAX,
		1.0f,
		VK_FALSE);

	imageQueue.push_function([&, device]() {
		vkDestroySampler(device, _defaultSamplerNearest, nullptr);
		vkDestroySampler(device, _defaultSamplerLinear, nullptr);
	});
}