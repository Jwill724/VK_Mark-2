#include "pch.h"

#include "ResourceManager.h"
#include "utils/BufferUtils.h"
#include "utils/VulkanUtils.h"
#include "renderer/Renderer.h"
#include "Environment.h"

namespace ResourceManager {
	ImageTableManager _globalImageManager;
	GPUEnvMapIndexArray _envMapIdxArray; // uniform
	glm::vec4 _ssaoKernelBlock[KERNEL_BLOCK_SIZE]{}; // uniform
	glm::vec4 _luminanceSums[MAX_LUMINANCE_GROUPS] = { glm::vec4(0.0f) }; // ssbo

	// primary render image
	AllocatedImage _opaqueImage;
	AllocatedImage& getOpaqueImage() { return _opaqueImage; }
	AllocatedImage _transparentImage;
	AllocatedImage& getTransparentImage() { return _transparentImage; }
	AllocatedImage _toneMapImage;
	AllocatedImage& getToneMapImage() { return _toneMapImage; }
	AllocatedImage _depthImage;
	AllocatedImage& getDepthImage() { return _depthImage; }
	AllocatedImage _msaaImage;
	AllocatedImage& getMSAAImage() { return _msaaImage; }

	AllocatedImage _depthResolvedImage;
	AllocatedImage& getDepthResolvedImage() { return _depthResolvedImage; }

	VkSampler _depthSampler;
	const VkSampler getDepthSampler() { return _depthSampler; }

	VkSampler _ssaoSampler;
	const VkSampler getSSAOSampler() { return _ssaoSampler; }

	VkSampler _normalSampler;
	const VkSampler getNormalSampler() { return _normalSampler; }

	VkSampler _noiseSampler;
	const VkSampler getNoiseSampler() { return _noiseSampler; }

	AllocatedImage _normalImage;
	AllocatedImage& getNormalImage() { return _normalImage; }

	// ssao images
	AllocatedImage _ssaoImage;
	AllocatedImage& getSSAOImage() { return _ssaoImage; }

	AllocatedImage _ssaoBlurHImage;
	AllocatedImage& getSSAOBlurHImage() { return _ssaoBlurHImage; }

	AllocatedImage _ssaoBlurVImage;
	AllocatedImage& getSSAOBlurVImage() { return _ssaoBlurVImage; }

	AllocatedImage _ssaoNoiseImage;
	AllocatedImage& getSSAONoiseImage() { return _ssaoNoiseImage; }

	AllocatedImage _shadowMapImage;
	AllocatedImage& getShadowMapImage() { return _shadowMapImage; }

	VkSampler _shadowMapSampler;
	const VkSampler getShadowMapSampler() { return _shadowMapSampler; }

	//std::vector<VkDescriptorSet> _shadowMapDescriptors; // Only meant for debugging visually in imgui
	//std::vector<VkDescriptorSet>& getShadowMapDescriptors() { return _shadowMapDescriptors; }

	// Grabbed during physical device selection
	std::vector<VkSampleCountFlags> _availableSampleCounts;
	std::vector<VkSampleCountFlags>& getAvailableSampleCounts() { return _availableSampleCounts; }


	// Textures
	AllocatedImage _whiteMat;
	AllocatedImage& getWhiteMat() { return _whiteMat; }

	AllocatedImage _metalRoughMat;
	AllocatedImage& getMetalRoughMat() { return _metalRoughMat; }

	AllocatedImage _emissiveMat;
	AllocatedImage& getEmissiveMat() { return _emissiveMat; }

	AllocatedImage _aoMat;
	AllocatedImage& getAOMat() { return _aoMat; }
	AllocatedImage _normalMat;
	AllocatedImage& getNormaMat() { return _normalMat; }
	AllocatedImage _errorCheckerboardTex;
	AllocatedImage& getCheckboardTex() { return _errorCheckerboardTex; }

	AllocatedImage _dummyTransparent;
	AllocatedImage& getDummyTransparent() { return _dummyTransparent; }

	VkSampler _defaultSamplerLinear;
	VkSampler _defaultSamplerNearest;
	const VkSampler getDefaultSamplerLinear() { return _defaultSamplerLinear; }
	const VkSampler getDefaultSamplerNearest() { return _defaultSamplerNearest; }


	AllocatedImage _skyboxImage;
	AllocatedImage& getSkyBoxImage() { return _skyboxImage; }

	VkSampler _skyBoxSampler;
	const VkSampler getSkyBoxSampler() { return _skyBoxSampler; }

	AllocatedImage _specularPrefilterImage;
	AllocatedImage& getSpecularPrefilterImage() { return _specularPrefilterImage; }

	VkSampler _specularPrefilterSampler;
	const VkSampler getSpecularPrefilterSampler() { return _specularPrefilterSampler; }

	AllocatedImage _irradianceImage;
	AllocatedImage& getIrradianceImage() { return _irradianceImage; }

	VkSampler _irradianceSampler;
	const VkSampler getIrradianceSampler() { return _irradianceSampler; }

	AllocatedImage _brdfLutImage;
	AllocatedImage& getBRDFImage() { return _brdfLutImage; }

	VkSampler _brdfSampler;
	const VkSampler getBRDFSampler() { return _brdfSampler; }
}


void GPUResources::init(const VkDevice device) {
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

	if (ssaoKernelBuffer.buffer != VK_NULL_HANDLE)
		BufferUtils::destroyAllocatedBuffer(ssaoKernelBuffer, allocator);

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

inline static float lerp(float a, float b, float f) {
	return a + f * (b - a);
}

// Opengl SSAO kernel implementation
void ResourceManager::initSSAOKernel() {
	std::uniform_real_distribution<float> randFloats(0.0f, 1.0f);
	std::default_random_engine rng;
	for (unsigned int i = 0; i < KERNEL_BLOCK_SIZE; i++) {
		glm::vec3 sample(
			randFloats(rng) * 2.0f - 1.0f, // x in [-1, 1]
			randFloats(rng) * 2.0f - 1.0f, // y in [-1, 1]
			randFloats(rng)                // z in [0, 1]
		);
		sample = glm::normalize(sample);
		sample *= randFloats(rng); // push inside hemisphere

		// bias: closer samples more dense, farther ones less
		float scale = static_cast<float>(i) / 64.0f;
		scale = lerp(0.1f, 0.9f, (scale * scale));
		sample *= scale;

		_ssaoKernelBlock[i] = glm::vec4(sample, 0.0f);
	}
}

void ResourceManager::initRenderTargets(
	const VkDevice device,
	DeletionQueue& targetQueue,
	const VmaAllocator allocator,
	const VkExtent3D drawExtent)
{
	// Opaque
	_opaqueImage.imageFormat = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
	_opaqueImage.imageExtent = drawExtent;

	VkImageUsageFlags drawImageUsages{};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;

	ImageUtils::createRenderImage(
		device,
		_opaqueImage,
		drawImageUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator);

	// Transparent
	_transparentImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	_transparentImage.imageExtent = drawExtent;

	ImageUtils::createRenderImage(
		device,
		_transparentImage,
		drawImageUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	// MSAA
	VkSampleCountFlagBits sampleCount = !MSAA_ENABLED ? VK_SAMPLE_COUNT_1_BIT : static_cast<VkSampleCountFlagBits>(CURRENT_MSAA_LVL);

	_msaaImage.imageFormat = _opaqueImage.imageFormat;
	_msaaImage.imageExtent = drawExtent;

	VkImageUsageFlags msaaImageUsages{};
	msaaImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	msaaImageUsages |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
	_msaaImage.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	ImageUtils::createRenderImage(
		device,
		_msaaImage,
		msaaImageUsages,
		sampleCount,
		targetQueue,
		allocator);

	// Base depth image
	_depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
	_depthImage.imageExtent = drawExtent;
	_depthImage.initialLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	VkImageUsageFlags depthImageUsages{};
	depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	ImageUtils::createRenderImage(
		device,
		_depthImage,
		depthImageUsages,
		sampleCount,
		targetQueue,
		allocator);

	// Depth resolved image
	_depthResolvedImage.imageFormat = VK_FORMAT_D32_SFLOAT;
	_depthResolvedImage.imageExtent = drawExtent;

	VkImageUsageFlags depthResolvedUsages{};
	depthResolvedUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	depthResolvedUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;
	ImageUtils::createRenderImage(
		device,
		_depthResolvedImage,
		depthResolvedUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator);

	// SSAO pass images

	VkImageUsageFlags ssaoUsages{};
	ssaoUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	ssaoUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;

	_ssaoImage.imageFormat = VK_FORMAT_R8_UNORM;
	_ssaoImage.imageExtent = drawExtent;

	// base ssao image
	ImageUtils::createRenderImage(
		device,
		_ssaoImage,
		ssaoUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator);

	// bi-lateral blur images
	_ssaoBlurHImage.imageFormat = VK_FORMAT_R8_UNORM;
	_ssaoBlurHImage.imageExtent = drawExtent;

	ImageUtils::createRenderImage(
		device,
		_ssaoBlurHImage,
		ssaoUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator);

	_ssaoBlurVImage.imageFormat = VK_FORMAT_R8_UNORM;
	_ssaoBlurVImage.imageExtent = drawExtent;

	ImageUtils::createRenderImage(
		device,
		_ssaoBlurVImage,
		ssaoUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator);

	// Normal
	_normalImage.imageFormat = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	_normalImage.imageExtent = drawExtent;

	VkImageUsageFlags normalUsages{};
	normalUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	normalUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;

	ImageUtils::createRenderImage(
		device,
		_normalImage,
		normalUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	// Tone map
	_toneMapImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	_toneMapImage.imageExtent = drawExtent;
	VkImageUsageFlags toneMapUsages{};
	toneMapUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	toneMapUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;
	toneMapUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	toneMapUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	ImageUtils::createRenderImage(
		device,
		_toneMapImage,
		toneMapUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);
}

void ResourceManager::initShadowMapImages(
	const VkDevice device,
	DeletionQueue& queue,
	const VmaAllocator allocator)
{
	// Shadow map image
	_shadowMapImage.imageFormat = VK_FORMAT_D32_SFLOAT;
	_shadowMapImage.imageExtent = { 4096, 4096, 1 };
	//_shadowMapImage.imageExtent = { 2048, 2048, 1 };
	_shadowMapImage.arrayLayers = MAX_CASCADES;

	VkImageUsageFlags shadowUsages =
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT;

	ImageUtils::createRenderImage(
		device,
		_shadowMapImage,
		shadowUsages,
		VK_SAMPLE_COUNT_1_BIT,
		queue,
		allocator
	);

	_shadowMapSampler = ImageUtils::createSampler(
		device,
		VK_FILTER_NEAREST,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		0.0f,
		1.0f,
		&queue,
		VK_SAMPLER_MIPMAP_MODE_NEAREST
	);

	//_shadowMapDescriptors.resize(MAX_CASCADES);
	//for (uint32_t i = 0; i < MAX_CASCADES; ++i) {
	//	_shadowMapDescriptors[i] = ImGui_ImplVulkan_AddTexture(
	//		_shadowMapSampler,
	//		_shadowMapImage.layerViews[i],
	//		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
	//	);
	//}

	//queue.push_function([=]() {
	//	for (auto ds : _shadowMapDescriptors) {
	//		if (ds != VK_NULL_HANDLE) {
	//			ImGui_ImplVulkan_RemoveTexture(ds);
	//		}
	//	}
	//});
}

void ResourceManager::initRenderSamplers(
	const VkDevice device,
	DeletionQueue& queue)
{
	_depthSampler = ImageUtils::createSampler(
		device,
		VK_FILTER_NEAREST,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		0.0f,
		1.0f,
		&queue,
		VK_SAMPLER_MIPMAP_MODE_NEAREST
	);

	_normalSampler = ImageUtils::createSampler(
		device,
		VK_FILTER_NEAREST,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		0.0f,
		1.0f,
		&queue,
		VK_SAMPLER_MIPMAP_MODE_NEAREST
	);

	_noiseSampler = ImageUtils::createSampler(
		device,
		VK_FILTER_NEAREST,
		VK_SAMPLER_ADDRESS_MODE_REPEAT,
		0.0f,
		1.0f,
		&queue,
		VK_SAMPLER_MIPMAP_MODE_NEAREST
	);

	_ssaoSampler = ImageUtils::createSampler(
		device,
		VK_FILTER_LINEAR,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		0.0f,
		1.0f,
		&queue,
		VK_SAMPLER_MIPMAP_MODE_NEAREST
	);
}

void ResourceManager::initEnvironmentImages(
	const VkDevice device,
	DeletionQueue& queue,
	const VmaAllocator allocator)
{
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
		0.0f,
		&queue);

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
		0.0f,
		&queue);


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
		&queue);


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
		&queue);
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


	_aoMat.imageExtent = texExtent;
	_aoMat.imageFormat = VK_FORMAT_R8_UNORM;
	_aoMat.mipmapped = true;

	uint8_t aoPixel = static_cast<uint8_t>(1.0f * 255); // full ambient lighting
	ImageUtils::createTextureImage(
		device,
		cmdPool,
		(void*)&aoPixel,
		_aoMat,
		usage,
		samples,
		imageQueue,
		bufferQueue,
		allocator);

	_normalMat.imageExtent = texExtent;
	_normalMat.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
	_normalMat.mipmapped = true;

	uint32_t flatNormal = glm::packUnorm4x8(glm::vec4(0.5f, 0.5f, 1.0f, 1.0f)); // X = 128, Y = 128, Z = 255, A = 255
	ImageUtils::createTextureImage(
		device,
		cmdPool,
		(void*)&flatNormal,
		_normalMat,
		usage,
		samples,
		imageQueue,
		bufferQueue,
		allocator);


	_emissiveMat.imageExtent = texExtent;
	_emissiveMat.imageFormat = format;
	_emissiveMat.mipmapped = true;

	uint32_t blackEmissive = glm::packUnorm4x8(glm::vec4(0, 0, 0, 1)); // No emission
	ImageUtils::createTextureImage(
		device,
		cmdPool,
		(void*)&blackEmissive,
		_emissiveMat,
		usage,
		samples,
		imageQueue,
		bufferQueue,
		allocator);

	_metalRoughMat.imageExtent = texExtent;
	_metalRoughMat.imageFormat = VK_FORMAT_R8G8B8A8_UNORM;
	_metalRoughMat.mipmapped = true;

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
		_metalRoughMat,
		usage,
		samples,
		imageQueue,
		bufferQueue,
		allocator);


	_dummyTransparent.imageExtent = texExtent;
	_dummyTransparent.imageFormat = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
	_dummyTransparent.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	glm::vec4 transparentBlack = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
	ImageUtils::createTextureImage(
		device,
		cmdPool,
		(void*)&transparentBlack,
		_dummyTransparent,
		VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		VK_SAMPLE_COUNT_1_BIT,
		imageQueue,
		bufferQueue,
		allocator
	);


	_whiteMat.imageExtent = texExtent;
	_whiteMat.imageFormat = format;
	_whiteMat.mipmapped = true;

	uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
	ImageUtils::createTextureImage(
		device,
		cmdPool,
		(void*)&white,
		_whiteMat,
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

	_errorCheckerboardTex.imageExtent = checkerboardedImageExtent;
	_errorCheckerboardTex.imageFormat = format;
	_errorCheckerboardTex.mipmapped = true;
	ImageUtils::createTextureImage(
		device,
		cmdPool,
		pixels.data(),
		_errorCheckerboardTex,
		usage,
		samples,
		imageQueue,
		bufferQueue,
		allocator);

	// SSAO noise texture
	std::vector<glm::vec2> noiseData;
	noiseData.reserve(16);

	std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
	std::default_random_engine rng;

	for (unsigned int i = 0; i < 16; i++) {
		glm::vec2 noise(dist(rng), dist(rng));
		noise = glm::normalize(noise); // keep it unit length
		noiseData.push_back(noise);
	}

	_ssaoNoiseImage.imageFormat = VK_FORMAT_R16G16_SFLOAT;
	_ssaoNoiseImage.imageExtent = { 4, 4, 1 };

	VkImageUsageFlags noiseUsages{};
	noiseUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;
	noiseUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	ImageUtils::createTextureImage(
		device,
		cmdPool,
		noiseData.data(),
		_ssaoNoiseImage,
		noiseUsages,
		samples,
		imageQueue,
		bufferQueue,
		allocator);

	// Default samplers
	_defaultSamplerLinear = ImageUtils::createSampler(
		device,
		VK_FILTER_LINEAR,
		VK_SAMPLER_ADDRESS_MODE_REPEAT,
		FLT_MAX,
		CURRENT_AF_LVL,
		&imageQueue);

	_defaultSamplerNearest = ImageUtils::createSampler(
		device,
		VK_FILTER_NEAREST,
		VK_SAMPLER_ADDRESS_MODE_REPEAT,
		FLT_MAX,
		1.0f,
		&imageQueue);
}