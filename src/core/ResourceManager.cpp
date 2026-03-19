#include "pch.h"

#include "ResourceManager.h"
#include "utils/BufferUtils.h"
#include "utils/VulkanUtils.h"
#include "renderer/Renderer.h"
#include "Environment.h"
#include "engine/engine.h"

static VkFormat BASE_DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;

namespace ResourceManager {
	ImageTableManager _globalImageManager;
	EnvironmentSet _environmentSets[MAX_ENV_SETS];
	GPUEnvMapIndexArray _envMapIdxArray; // uniform
	glm::vec4 _luminanceSums[MAX_LUMINANCE_GROUPS] = { glm::vec4(0.0f) }; // ssbo

	// primary render image
	AllocatedImage _opaqueImage;
	AllocatedImage& getOpaqueImage() { return _opaqueImage; }
	AllocatedImage _transparentImage;
	AllocatedImage& getTransparentImage() { return _transparentImage; }
	AllocatedImage _toneMapImage;
	AllocatedImage& getToneMapImage() { return _toneMapImage; }

	AllocatedImage _depthResolvedImage;
	AllocatedImage& getDepthResolvedImage() { return _depthResolvedImage; }

	AllocatedImage _depthImage;
	AllocatedImage& getDepthImage() { return _depthImage; }

	AllocatedImage _prevDepthResolvedImage;
	AllocatedImage& getPrevDepthResolvedImage() { return _prevDepthResolvedImage; }

	AllocatedImage _prevVelocityImage;
	AllocatedImage& getPrevVelocityImage() { return _prevVelocityImage; }

	// Max lod = mip count
	VkSampler _hiZSampler;
	const VkSampler getHiZSampler() { return _hiZSampler; }

	AllocatedImage _hiZ;
	AllocatedImage& getHiZ() { return _hiZ; }

	VkSampler _linearLODClampSampler;
	const VkSampler getLinearLODClampSampler() { return _linearLODClampSampler; }

	VkSampler _pointBorderSampler;
	const VkSampler getPointBorderSampler() { return _pointBorderSampler; }

	VkSampler _taaHistorySampler;
	const VkSampler getTaaHistorySampler() { return _taaHistorySampler; }

	VkSampler _noiseSampler;
	const VkSampler getNoiseSampler() { return _noiseSampler; }

	AllocatedImage _viewSpaceNormals;
	AllocatedImage& getViewSpaceNormals() { return _viewSpaceNormals; }

	AllocatedImage _bentNormals;
	AllocatedImage& getBentNormals() { return _bentNormals; }

	// ao images
	AllocatedImage _edgeInfoImage;
	AllocatedImage& getEdgeInfoImage() { return _edgeInfoImage; }
	AllocatedImage _aoRawImage;
	AllocatedImage& getAORawImage() { return _aoRawImage; }
	AllocatedImage _aoTempImage;
	AllocatedImage& getAOTempImage() { return _aoTempImage; }

	// COLOR HISTORY
	uint32_t _colorHistoryIndex = 0;
	AllocatedImage _colorHistoryImages[2];
	AllocatedImage& getColorHistoryRead() {
		return _colorHistoryImages[_colorHistoryIndex];
	}
	AllocatedImage& getColorHistoryWrite() {
		return _colorHistoryImages[_colorHistoryIndex ^ 1u];
	}
	void flipColorHistory() { _colorHistoryIndex ^= 1u; }
	void resetColorHistoryIndex() { _colorHistoryIndex = 0; }

	AllocatedImage _aaColor;
	AllocatedImage& getAAColor() { return _aaColor; }

	AllocatedImage _postNonAAComposite;
	AllocatedImage& getPostNonAAComposite() { return _postNonAAComposite; }

	AllocatedImage _cmaa2WorkingEdges;
	AllocatedImage& getCMAA2WorkingEdges() { return _cmaa2WorkingEdges; }

	AllocatedImage _areaTex;
	AllocatedImage& getAreaTex() { return _areaTex; }
	AllocatedImage _searchTex;
	AllocatedImage& getSearchTex() { return _searchTex; }
	AllocatedImage _smaaEdges;
	AllocatedImage& getSMAAEdges() { return _smaaEdges; }
	AllocatedImage _smaaWeights;
	AllocatedImage& getSMAAWeights() { return _smaaWeights; }

	AllocatedImage _flareBrightImage;
	AllocatedImage& getFlareBrightImage() { return _flareBrightImage; }

	AllocatedImage _lensFlareColorImage;
	AllocatedImage& getLensFlareColorImage() { return _lensFlareColorImage; }

	AllocatedImage _rainbowLUTImage;
	AllocatedImage& getRainbowLUTImage() { return _rainbowLUTImage; }

	AllocatedImage _velocityImage;
	AllocatedImage& getVelocityImage() { return _velocityImage; }

	AllocatedImage _volumetricLightImage;
	AllocatedImage& getVolumetricLightImage() { return _volumetricLightImage; }

	AllocatedImage _volumetricBlurImage;
	AllocatedImage& getVolumetricBlurImage() { return _volumetricBlurImage; }

	//AllocatedImage _4x4NoiseImage;
	//AllocatedImage& get4x4NoiseImage() { return _4x4NoiseImage; }

	AllocatedImage _directionalCSMAtlas;
	AllocatedImage& getDirectionalCSMAtlas() { return _directionalCSMAtlas; }

	VkSampler _shadowMapSampler;
	const VkSampler getShadowMapSampler() { return _shadowMapSampler; }

	AllocatedImage _screenSpaceShadowMask;
	AllocatedImage& getScreenSpaceShadowMask() { return _screenSpaceShadowMask; }

	AllocatedImage _flashLightShadowMap;
	AllocatedImage& getFlashLightShadowMap() { return _flashLightShadowMap; }

	AllocatedImage _cookieGoboImage;
	AllocatedImage& getCookieGoboImage() { return _cookieGoboImage; }

	AllocatedImage _dummyUint8;
	AllocatedImage& getDummyUint8() { return _dummyUint8; }

	//std::vector<VkDescriptorSet> _shadowMapDescriptors; // Only meant for debugging visually in imgui
	//std::vector<VkDescriptorSet>& getShadowMapDescriptors() { return _shadowMapDescriptors; }


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

	AllocatedImage _dummyImage;
	AllocatedImage& getDummyImage() { return _dummyImage; }

	VkSampler _defaultSamplerLinear;
	const VkSampler getDefaultSamplerLinear() { return _defaultSamplerLinear; }

	VkSampler _defaultSamplerNearest;
	const VkSampler getDefaultSamplerNearest() { return _defaultSamplerNearest; }

	VkSampler _nearestClampSampler;
	const VkSampler getNearestClampSampler() { return _nearestClampSampler; }

	VkSampler _linearClampSampler;
	const VkSampler getLinearClampSampler() { return _linearClampSampler; }

	VkSampler _skyBoxSampler;
	const VkSampler getSkyBoxSampler() { return _skyBoxSampler; }

	VkSampler _specularPrefilterSampler;
	const VkSampler getSpecularPrefilterSampler() { return _specularPrefilterSampler; }

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

void GPUResources::updateAddressTableMapped() {
	std::scoped_lock lock(addressTableMutex);

	if (!gpuAddresses.isTableDirty()) return;

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
	ASSERT(transferPool != VK_NULL_HANDLE);

	const auto device = Backend::getDevice();

	CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {
		VkBufferCopy copyRegion{};
		copyRegion.size = sizeof(GPUAddressTable);
		vkCmdCopyBuffer(cmd, addressTableStagingBuffer.buffer, addressTableBuffer.buffer, 1, &copyRegion);
	}, transferPool, QueueType::Transfer, device);

	auto& tQueue = Backend::getTransferQueue();
	lastSubmittedFence = Engine::getState().submitCommandBuffers(tQueue);
	waitAndRecycleLastFence(lastSubmittedFence, tQueue, device);

	gpuAddresses.clearTableDirty();
}

void GPUResources::cleanup(VkDevice device) {
	for (uint32_t i = 0; i < static_cast<uint32_t>(AddressBufferType::Count); ++i) {
		AddressBufferType bufferType = static_cast<AddressBufferType>(i);
		gpuAddresses.removeAddress(bufferType);
	}

	for (auto& [name, buf] : gpuBuffers) {
		if (buf.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(buf, allocator);
	}

	materialFlagsIDs.clear();


	if (lightListStagingBuffer.buffer != VK_NULL_HANDLE)
		BufferUtils::destroyAllocatedBuffer(lightListStagingBuffer, allocator);

	if (instanceTransformsStagingBuffer.buffer != VK_NULL_HANDLE)
		BufferUtils::destroyAllocatedBuffer(instanceTransformsStagingBuffer, allocator);

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
}

void ResourceManager::initUniformRenderTargets(
	const VkDevice device,
	DeletionQueue& targetQueue,
	const VmaAllocator allocator,
	const VkExtent3D drawExtent)
{
	VkImageUsageFlags baseComputeUsages{};
	baseComputeUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	baseComputeUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;
	baseComputeUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	baseComputeUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

	VkImageUsageFlags drawImageUsages{};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;

	VkImageUsageFlags computeMinimalUsages{};
	computeMinimalUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	computeMinimalUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;

	VkImageUsageFlags mrtColorUsages{};
	mrtColorUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	mrtColorUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;

	VkExtent3D halfExtent = {
		(drawExtent.width + 1u) >> 1,
		(drawExtent.height + 1u) >> 1,
		1u
	};
	VkExtent3D quarterExtent = {
		(drawExtent.width + 3u) >> 2,
		(drawExtent.height + 3u) >> 2,
		1u
	};

	// Opaque
	//_opaqueImage.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
	_opaqueImage.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	_opaqueImage.extent = drawExtent;

	ImageUtils::createRenderImage(
		device,
		_opaqueImage,
		drawImageUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator);

	// Transparent
	_transparentImage.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	_transparentImage.extent = drawExtent;

	ImageUtils::createRenderImage(
		device,
		_transparentImage,
		drawImageUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	// Depth resolved image
	_depthResolvedImage.format = BASE_DEPTH_FORMAT;
	_depthResolvedImage.extent = drawExtent;
	VkImageUsageFlags depthResolvedUsages{};
	depthResolvedUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	depthResolvedUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;
	depthResolvedUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	depthResolvedUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	ImageUtils::createRenderImage(
		device,
		_depthResolvedImage,
		depthResolvedUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator);
	_prevDepthResolvedImage.format = BASE_DEPTH_FORMAT;
	_prevDepthResolvedImage.extent = drawExtent;
	ImageUtils::createRenderImage(
		device,
		_prevDepthResolvedImage,
		baseComputeUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator);

	_depthImage.format = BASE_DEPTH_FORMAT;
	_depthImage.extent = drawExtent;
	ImageUtils::createRenderImage(
		device,
		_depthImage,
		depthResolvedUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator);

	// base ao image
	_aoRawImage.format = VK_FORMAT_R8_UNORM;
	_aoRawImage.extent = drawExtent;
	ImageUtils::createRenderImage(
		device,
		_aoRawImage,
		computeMinimalUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator);

	// ao temp
	_aoTempImage.format = VK_FORMAT_R8_UNORM;
	_aoTempImage.extent = drawExtent;
	ImageUtils::createRenderImage(
		device,
		_aoTempImage,
		computeMinimalUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator);

	// color history images
	resetColorHistoryIndex();
	for (uint32_t i = 0; i < 2; ++i) {
		_colorHistoryImages[i].format = VK_FORMAT_R16G16B16A16_SFLOAT;
		_colorHistoryImages[i].extent = drawExtent;

		ImageUtils::createRenderImage(
			device,
			_colorHistoryImages[i],
			baseComputeUsages,
			VK_SAMPLE_COUNT_1_BIT,
			targetQueue,
			allocator);
	}

	// Edge image
	_edgeInfoImage.format = VK_FORMAT_R8_UNORM;
	_edgeInfoImage.extent = drawExtent;
	ImageUtils::createRenderImage(
		device,
		_edgeInfoImage,
		computeMinimalUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	// Velocity
	_velocityImage.format = VK_FORMAT_R16G16_SFLOAT;
	_velocityImage.extent = drawExtent;
	ImageUtils::createRenderImage(
		device,
		_velocityImage,
		drawImageUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);
	_prevVelocityImage.format = VK_FORMAT_R16G16_SFLOAT;
	_prevVelocityImage.extent = drawExtent;
	ImageUtils::createRenderImage(
		device,
		_prevVelocityImage,
		baseComputeUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);


	// View space normals
	_viewSpaceNormals.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	_viewSpaceNormals.extent = drawExtent;
	ImageUtils::createRenderImage(
		device,
		_viewSpaceNormals,
		drawImageUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	// Tone map
	_toneMapImage.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	_toneMapImage.extent = drawExtent;
	ImageUtils::createRenderImage(
		device,
		_toneMapImage,
		baseComputeUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	// Depth pyramid
	_hiZ.format = VK_FORMAT_R32_UINT;
	_hiZ.extent = drawExtent;
	_hiZ.mipLevelCount = HI_Z_MIP_COUNT;
	_hiZ.perMipStorageViews = true;
	ImageUtils::createRenderImage(
		device,
		_hiZ,
		baseComputeUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	// Volumetric light images
	_volumetricLightImage.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	_volumetricLightImage.extent = halfExtent;
	ImageUtils::createRenderImage(
		device,
		_volumetricLightImage,
		computeMinimalUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);
	_volumetricBlurImage.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	_volumetricBlurImage.extent = halfExtent;
	ImageUtils::createRenderImage(
		device,
		_volumetricBlurImage,
		computeMinimalUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);


	// Lens flare images
	_flareBrightImage.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	_flareBrightImage.extent = quarterExtent;
	ImageUtils::createRenderImage(
		device,
		_flareBrightImage,
		computeMinimalUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);
	_lensFlareColorImage.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	_lensFlareColorImage.extent = quarterExtent;
	ImageUtils::createRenderImage(
		device,
		_lensFlareColorImage,
		computeMinimalUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	// AA images

	_aaColor.extent = drawExtent;
	_aaColor.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	ImageUtils::createRenderImage(
		device,
		_aaColor,
		baseComputeUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	_cmaa2WorkingEdges.extent = { halfExtent.width, drawExtent.height, 1u };
	_cmaa2WorkingEdges.format = VK_FORMAT_R8_UINT;
	ImageUtils::createRenderImage(
		device,
		_cmaa2WorkingEdges,
		baseComputeUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	_smaaEdges.extent = drawExtent;
	_smaaEdges.format = VK_FORMAT_R8G8_UNORM;
	ImageUtils::createRenderImage(
		device,
		_smaaEdges,
		computeMinimalUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	_smaaWeights.extent = drawExtent;
	_smaaWeights.format = VK_FORMAT_R8G8B8A8_UNORM;
	ImageUtils::createRenderImage(
		device,
		_smaaWeights,
		computeMinimalUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	_screenSpaceShadowMask.extent = drawExtent;
	_screenSpaceShadowMask.format = VK_FORMAT_R8_UNORM;
	ImageUtils::createRenderImage(
		device,
		_screenSpaceShadowMask,
		computeMinimalUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	// Bent normals
	_bentNormals.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	_bentNormals.extent = drawExtent;
	ImageUtils::createRenderImage(
		device,
		_bentNormals,
		baseComputeUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	// post non aa composite
	_postNonAAComposite.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	_postNonAAComposite.extent = drawExtent;
	ImageUtils::createRenderImage(
		device,
		_postNonAAComposite,
		baseComputeUsages,
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
	VkExtent3D csmExtent = { 4096, 4096, 1 };
	//VkExtent3D csmExtent = { 2048, 2048, 1 };
	//VkExtent3D csmExtent = { 8192, 8192, 1 };

	// Directional cascaded shadow map atlas
	_directionalCSMAtlas.format = BASE_DEPTH_FORMAT;
	_directionalCSMAtlas.extent = csmExtent;

	VkImageUsageFlags shadowUsages =
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT;

	ImageUtils::createRenderImage(
		device,
		_directionalCSMAtlas,
		shadowUsages,
		VK_SAMPLE_COUNT_1_BIT,
		queue,
		allocator
	);

	// Flashlight shadow
	VkExtent3D flashlightExtent = { 512, 512, 1 };
	_flashLightShadowMap.extent = flashlightExtent;
	_flashLightShadowMap.format = BASE_DEPTH_FORMAT;

	ImageUtils::createRenderImage(
		device,
		_flashLightShadowMap,
		shadowUsages,
		VK_SAMPLE_COUNT_1_BIT,
		queue,
		allocator
	);

	_shadowMapSampler = ImageUtils::createSampler(
		device,
		VK_FILTER_LINEAR,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		0.0f,
		1.0f,
		&queue,
		VK_SAMPLER_MIPMAP_MODE_NEAREST
	);


	//_shadowMapDescriptors.resize(MAX_SHADOW_CASCADES);
	//for (uint32_t i = 0; i < MAX_SHADOW_CASCADES; ++i) {
	//	_shadowMapDescriptors[i] = ImGui_ImplVulkan_AddTexture(
	//		_shadowMapSampler,
	//		_directionalCSMAtlas.layerViews[i],
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
	_hiZSampler = ImageUtils::createSampler(
		device,
		VK_FILTER_NEAREST,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		static_cast<float>(HI_Z_MIP_COUNT - 1),
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

	_linearLODClampSampler = ImageUtils::createSampler(
		device,
		VK_FILTER_LINEAR,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		0.0f,
		1.0f,
		&queue,
		VK_SAMPLER_MIPMAP_MODE_NEAREST
	);

	// Default samplers

	// *Need to recreate due to having a set af level
	_defaultSamplerLinear = ImageUtils::createSampler(
		device,
		VK_FILTER_LINEAR,
		VK_SAMPLER_ADDRESS_MODE_REPEAT,
		FLT_MAX,
		CURRENT_AF_LVL,
		&queue);

	_defaultSamplerNearest = ImageUtils::createSampler(
		device,
		VK_FILTER_NEAREST,
		VK_SAMPLER_ADDRESS_MODE_REPEAT,
		FLT_MAX,
		1.0f,
		&queue);

	_nearestClampSampler = ImageUtils::createSampler(
		device,
		VK_FILTER_NEAREST,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		FLT_MAX,
		1.0f,
		&queue,
		VK_SAMPLER_MIPMAP_MODE_NEAREST
	);

	_linearClampSampler = ImageUtils::createSampler(
		device,
		VK_FILTER_LINEAR,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		FLT_MAX,
		1.0f,
		&queue,
		VK_SAMPLER_MIPMAP_MODE_LINEAR
	);

	_pointBorderSampler = ImageUtils::createSampler(
		device,
		VK_FILTER_NEAREST,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
		0.0,
		1.0f,
		&queue,
		VK_SAMPLER_MIPMAP_MODE_NEAREST
	);

	_taaHistorySampler = ImageUtils::createSampler(
		device,
		VK_FILTER_LINEAR,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		0.0f,
		1.0f,
		&queue,
		VK_SAMPLER_MIPMAP_MODE_NEAREST
);
}

EnvironmentSet ResourceManager::initEnvironmentSetImages(
	const VkDevice device,
	DeletionQueue& queue,
	const VmaAllocator allocator)
{
	EnvironmentSet env{};

	VkImageUsageFlags usage =
		VK_IMAGE_USAGE_STORAGE_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT;
	VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
	VkFormat environmentFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

	// SKYBOX
	env.skybox.extent = Environment::SKYBOX_EXTENTS;
	env.skybox.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
	env.skybox.isCubeMap = true;
	env.skybox.mipmapped = true;

	ImageUtils::createRenderImage(
		device,
		env.skybox,
		usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		samples,
		queue,
		allocator);

	// SPECULAR
	env.specular.extent = Environment::SPECULAR_EXTENTS;
	env.specular.format = environmentFormat;
	env.specular.isCubeMap = true;
	env.specular.perMipStorageViews = true;
	env.specular.mipLevelCount = Environment::SPECULAR_PREFILTERED_MIP_LEVELS;

	ImageUtils::createRenderImage(
		device,
		env.specular,
		usage,
		samples,
		queue,
		allocator);

	// IRRADIANCE
	env.irradiance.extent = Environment::DIFFUSE_IRRADIANCE_BASE_EXTENTS;
	env.irradiance.format = environmentFormat;
	env.irradiance.isCubeMap = true;

	ImageUtils::createRenderImage(
		device,
		env.irradiance,
		usage,
		samples,
		queue,
		allocator);

	return env;
}

void ResourceManager::initStaticEnvironmentImages(
	const VkDevice device,
	DeletionQueue& queue,
	const VmaAllocator allocator)
{
	_skyBoxSampler = ImageUtils::createSampler(
		device,
		VK_FILTER_LINEAR,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		0.0f,
		0.0f,
		&queue);

	_specularPrefilterSampler = ImageUtils::createSampler(
		device,
		VK_FILTER_LINEAR,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		static_cast<float>(Environment::SPECULAR_PREFILTERED_MIP_LEVELS - 1),
		0.0f,
		&queue);

	_irradianceSampler = ImageUtils::createSampler(
		device,
		VK_FILTER_LINEAR,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		VK_LOD_CLAMP_NONE,
		0.0f,
		&queue);


	_brdfLutImage.extent = Environment::LUT_IMAGE_EXTENT;
	_brdfLutImage.format = VK_FORMAT_R16G16_SFLOAT;

	ImageUtils::createRenderImage(
		device,
		_brdfLutImage,
		VK_IMAGE_USAGE_STORAGE_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_SAMPLE_COUNT_1_BIT,
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

static glm::vec3 hsvToRgb(const float hue01, const float sat, const float val)
{
	const float hue = hue01 - floor(hue01); // wrap [0,1)
	const float c = val * sat;
	const float h6 = hue * 6.0f;
	const float x = c * (1.0f - fabsf(fmodf(h6, 2.0f) - 1.0f));
	const float m = val - c;

	glm::vec3 rgb(0.0f);

	if (h6 < 1.0f) rgb = glm::vec3(c, x, 0.0f);
	else if (h6 < 2.0f) rgb = glm::vec3(x, c, 0.0f);
	else if (h6 < 3.0f) rgb = glm::vec3(0.0f, c, x);
	else if (h6 < 4.0f) rgb = glm::vec3(0.0f, x, c);
	else if (h6 < 5.0f) rgb = glm::vec3(x, 0.0f, c);
	else rgb = glm::vec3(c, 0.0f, x);

	return rgb + glm::vec3(m);
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


	_aoMat.extent = texExtent;
	_aoMat.format = VK_FORMAT_R8_UNORM;
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

	_normalMat.extent = texExtent;
	_normalMat.format = VK_FORMAT_R8G8B8A8_UNORM;
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


	_emissiveMat.extent = texExtent;
	_emissiveMat.format = format;
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

	_metalRoughMat.extent = texExtent;
	_metalRoughMat.format = VK_FORMAT_R8G8B8A8_UNORM;
	_metalRoughMat.mipmapped = true;

	// From what I've read about modern GLTF pbr, g is roughness and b is metallic.
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


	_dummyImage.extent = texExtent;
	_dummyImage.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
	//_dummyImage.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	glm::vec4 black = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
	ImageUtils::createTextureImage(
		device,
		cmdPool,
		(void*)&black,
		_dummyImage,
		usage,
		VK_SAMPLE_COUNT_1_BIT,
		imageQueue,
		bufferQueue,
		allocator
	);

	_whiteMat.extent = texExtent;
	_whiteMat.format = format;
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

	_errorCheckerboardTex.extent = checkerboardedImageExtent;
	_errorCheckerboardTex.format = format;
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


	//// 4x4noise texture
	//std::vector<glm::vec2> noise4x4Data;
	//noise4x4Data.reserve(16);

	//std::uniform_real_distribution<float> dist4x4(-1.0f, 1.0f);
	//std::default_random_engine rng4x4;

	//for (uint32_t i = 0; i < 16; ++i) {
	//	glm::vec2 noise(
	//		dist4x4(rng4x4),
	//		dist4x4(rng4x4)
	//	);

	//	noise4x4Data.push_back(noise);
	//}

	//_4x4NoiseImage.format = VK_FORMAT_R16G16_SFLOAT;
	//_4x4NoiseImage.extent = { 4, 4, 1 };

	//ImageUtils::createTextureImage(
	//	device,
	//	cmdPool,
	//	noise4x4Data.data(),
	//	_4x4NoiseImage,
	//	usage,
	//	samples,
	//	imageQueue,
	//	bufferQueue,
	//	allocator);

	// Rainbow lut
	const uint32_t lutWidth = 256u;
	const uint32_t lutHeight = 1u;

	std::vector<uint32_t> lutPixels;
	lutPixels.resize(static_cast<size_t>(lutWidth) * lutHeight);

	for (uint32_t x = 0; x < lutWidth; ++x) {
		const float t = static_cast<float>(x) / static_cast<float>(lutWidth - 1);

		float hue = 0.02f + 0.90f * t;
		hue = hue - 0.06f;
		hue = hue - std::floor(hue);

		glm::vec3 rgb = hsvToRgb(hue, 0.95f, 1.0f);
		rgb.g *= 0.9f;

		const glm::vec4 rgba(rgb, 1.0);
		lutPixels[static_cast<size_t>(x)] = glm::packUnorm4x8(rgba);
	}


	_rainbowLUTImage.format = VK_FORMAT_R8G8B8A8_UNORM;
	_rainbowLUTImage.extent = { lutWidth, lutHeight, 1u };

	ImageUtils::createTextureImage(
		device,
		cmdPool,
		lutPixels.data(),
		_rainbowLUTImage,
		usage,
		samples,
		imageQueue,
		bufferQueue,
		allocator
	);


	int cookieW = 0;
	int cookieH = 0;
	int channelsCookie = 0;

	stbi_uc* cookieData = stbi_load(
		"res/assets/light_cookie.png",
		&cookieW,
		&cookieH,
		&channelsCookie,
		1
	);
	if (!cookieData) {
		fmt::print("Failed to load Cookie texture: {}\n", stbi_failure_reason());
		ASSERT(true);
	}
	_cookieGoboImage.extent = { 512, 512, 1 };
	_cookieGoboImage.format = VK_FORMAT_R8_UNORM;

	ImageUtils::createTextureImage(
		device,
		cmdPool,
		cookieData,
		_cookieGoboImage,
		usage,
		samples,
		imageQueue,
		bufferQueue,
		allocator
	);

	stbi_image_free(cookieData);


	_areaTex.extent = { AREATEX_WIDTH, AREATEX_HEIGHT, 1 };
	_areaTex.format = VK_FORMAT_R8G8_UNORM;

	ImageUtils::createTextureImage(
		device,
		cmdPool,
		areaTexBytes,
		_areaTex,
		usage,
		samples,
		imageQueue,
		bufferQueue,
		allocator
	);

	_searchTex.extent = { SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 1 };
	_searchTex.format = VK_FORMAT_R8_UNORM;

	ImageUtils::createTextureImage(
		device,
		cmdPool,
		searchTexBytes,
		_searchTex,
		usage,
		samples,
		imageQueue,
		bufferQueue,
		allocator
	);


	_dummyUint8.format = VK_FORMAT_R8_UINT;
	_dummyUint8.extent = texExtent;
	ImageUtils::createRenderImage(
		device,
		_dummyUint8,
		usage,
		samples,
		imageQueue,
		allocator
	);
}
