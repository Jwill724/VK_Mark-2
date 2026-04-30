#include "pch.h"

#include "ResourceManager.h"
#include "utils/BufferUtils.h"
#include "utils/VulkanUtils.h"
#include "renderer/Renderer.h"
#include "Environment.h"
#include "engine/engine.h"

static VkFormat BASE_DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;

namespace ResourceManager
{
	ImageTableManager _globalImageManager;
	EnvironmentSet _environmentSets[MAX_ENV_SETS];
	EnvironmentIndexArray _envMapIdxArray; // uniform
	glm::vec4 _luminanceSums[MAX_LUMINANCE_GROUPS] = { glm::vec4(0.0f) }; // ssbo

	// primary render image
	AllocatedImage _opaque;
	AllocatedImage& GetOpaque_Target() { return _opaque; }

	AllocatedImage _transparentResolved;
	AllocatedImage& GetTransparentResolved_Target() { return _transparentResolved; }
	// For OIT
	AllocatedImage _transparentAccumulation;
	AllocatedImage& GetTransparentAccumulation_Target() { return _transparentAccumulation; }
	AllocatedImage _transparentRevealage;
	AllocatedImage& GetTransparentRevealage_Target() { return _transparentRevealage; }

	AllocatedImage _toneMap;
	AllocatedImage& GetToneMap_Target() { return _toneMap; }

	AllocatedImage _depthResolved;
	AllocatedImage& GetDepthResolved_Target() { return _depthResolved; }

	AllocatedImage _depthRaw;
	AllocatedImage& GetDepthRaw_Target() { return _depthRaw; }

	AllocatedImage _prevDepthResolved;
	AllocatedImage& GetPrevDepthResolved_Target() { return _prevDepthResolved; }

	AllocatedImage _prevVelocity;
	AllocatedImage& GetPrevVelocity_Target() { return _prevVelocity; }

	AllocatedImage _viewSpaceNormals;
	AllocatedImage& GetViewSpaceNormals_Target() { return _viewSpaceNormals; }

	// Max lod = mip count
	VkSampler _hiZSampler;
	const VkSampler GetHiZ_Sampler() { return _hiZSampler; }

	AllocatedImage _hiZ;
	AllocatedImage& GetHiZ_Target() { return _hiZ; }

	AllocatedImage _linearizedMinHiZ;
	AllocatedImage& GetLinearizedMinHiZ_Target() { return _linearizedMinHiZ; }

	VkSampler _linearLODClampSampler;
	const VkSampler GetLinearLODClamp_Sampler() { return _linearLODClampSampler; }

	VkSampler _pointBorderSampler;
	const VkSampler GetPointBorder_Sampler() { return _pointBorderSampler; }

	VkSampler _taaHistorySampler;
	const VkSampler GetTaaHistory_Sampler() { return _taaHistorySampler; }

	VkSampler _noiseSampler;
	const VkSampler GetNoise_Sampler() { return _noiseSampler; }

	AllocatedImage _bentNormals;
	AllocatedImage& GetBentNormals_Target() { return _bentNormals; }

	// ao images
	AllocatedImage _aoEdgeInfo;
	AllocatedImage& GetAOEdgeInfo_Target() { return _aoEdgeInfo; }
	AllocatedImage _aoRaw;
	AllocatedImage& GetAORaw_Target() { return _aoRaw; }
	AllocatedImage _aoTemp;
	AllocatedImage& GetAOTemp_Target() { return _aoTemp; }

	// COLOR HISTORY
	uint32_t _colorHistoryIndex = 0;
	AllocatedImage _colorHistory[2];
	AllocatedImage& GetColorHistoryRead_Target() {
		return _colorHistory[_colorHistoryIndex];
	}
	AllocatedImage& GetColorHistoryWrite_Target() {
		return _colorHistory[_colorHistoryIndex ^ 1u];
	}
	void FlipColorHistory() { _colorHistoryIndex ^= 1u; }
	void ResetColorHistoryIndex() { _colorHistoryIndex = 0; }

	AllocatedImage _aaColor;
	AllocatedImage& GetAAColor_Target() { return _aaColor; }

	AllocatedImage _postNonAAComposite;
	AllocatedImage& GetPostNonAAComposite_Target() { return _postNonAAComposite; }

	AllocatedImage _cmaa2WorkingEdges;
	AllocatedImage& GetCMAA2WorkingEdges_Target() { return _cmaa2WorkingEdges; }

	AllocatedImage _smaaEdges;
	AllocatedImage& GetSMAAEdges_Target() { return _smaaEdges; }
	AllocatedImage _smaaWeights;
	AllocatedImage& GetSMAAWeights_Target() { return _smaaWeights; }

	AllocatedImage _flareBright;
	AllocatedImage& GetFlareBright_Target() { return _flareBright; }

	AllocatedImage _lensFlareColor;
	AllocatedImage& GetLensFlareColor_Target() { return _lensFlareColor; }


	AllocatedImage _velocity;
	AllocatedImage& GetVelocity_Target() { return _velocity; }

	AllocatedImage _volumetricLight;
	AllocatedImage& GetVolumetricLight_Target() { return _volumetricLight; }

	AllocatedImage _volumetricBlur;
	AllocatedImage& GetVolumetricBlur_Target() { return _volumetricBlur; }

	AllocatedImage _directionalCSMAtlas;
	AllocatedImage& GetDirectionalCSMAtlas_Target() { return _directionalCSMAtlas; }

	VkSampler _shadowMapSampler;
	const VkSampler GetShadowMap_Sampler() { return _shadowMapSampler; }

	AllocatedImage _screenSpaceShadowMask;
	AllocatedImage& GetScreenSpaceShadowMask_Target() { return _screenSpaceShadowMask; }

	AllocatedImage _flashLightShadowMap;
	AllocatedImage& GetFlashlightShadowMap_Target() { return _flashLightShadowMap; }

	AllocatedImage _areaSMAATexture;
	AllocatedImage& GetAreaSMAA_Texture() { return _areaSMAATexture; }
	AllocatedImage _searchSMAATexture;
	AllocatedImage& GetSearchSMAA_Texture() { return _searchSMAATexture; }

	AllocatedImage _cookieGoboTexture;
	AllocatedImage& GetCookieGobo_Texture() { return _cookieGoboTexture; }

	AllocatedImage _rainbowLUTTexture;
	AllocatedImage& GetRainbowLUT_Texture() { return _rainbowLUTTexture; }

	AllocatedImage _dummyUint8Texture;
	AllocatedImage& GetDummyUint8_Texture() { return _dummyUint8Texture; }

	AllocatedImage _whiteTexture;
	AllocatedImage& GetWhiteMat_Texture() { return _whiteTexture; }

	AllocatedImage _metalRoughTexture;
	AllocatedImage& GetMetalRough_Texture() { return _metalRoughTexture; }

	AllocatedImage _emissiveTexture;
	AllocatedImage& GetEmissive_Texture() { return _emissiveTexture; }

	AllocatedImage _normalTexture;
	AllocatedImage& GetNormal_Texture() { return _normalTexture; }
	AllocatedImage _errorCheckerboardTexture;
	AllocatedImage& GetCheckboard_Texture() { return _errorCheckerboardTexture; }

	AllocatedImage _hilbertCurveLUT;
	AllocatedImage& GetHilbertCurveLUT_Texture() { return _hilbertCurveLUT; } 

	AllocatedImage _dummyTexture;
	AllocatedImage& GetDummy_Texture() { return _dummyTexture; }

	VkSampler _defaultLinearSampler;
	const VkSampler GetDefaultLinear_Sampler() { return _defaultLinearSampler; }

	VkSampler _defaultNearestSampler;
	const VkSampler GetDefaultNearest_Sampler() { return _defaultNearestSampler; }

	VkSampler _nearestClampSampler;
	const VkSampler GetNearestClamp_Sampler() { return _nearestClampSampler; }

	VkSampler _linearClampSampler;
	const VkSampler GetLinearClamp_Sampler() { return _linearClampSampler; }

	VkSampler _skyBoxSampler;
	const VkSampler GetSkyBox_Sampler() { return _skyBoxSampler; }

	VkSampler _specularPrefilterSampler;
	const VkSampler GetSpecularPrefilter_Sampler() { return _specularPrefilterSampler; }

	VkSampler _irradianceSampler;
	const VkSampler GetIrradiance_Sampler() { return _irradianceSampler; }

	AllocatedImage _brdfLut;
	AllocatedImage& GetBRDF_Texture() { return _brdfLut; }

	VkSampler _brdfSampler;
	const VkSampler GetBRDF_Sampler() { return _brdfSampler; }
}


void GPUResources::Init(const VkDevice device) {
	allocator = VulkanUtils::CreateAllocator(Backend::GetPhysicalDevice(), device, Backend::GetInstance());
	graphicsPool = CommandBuffer::CreateCommandPool(device, Backend::GetGraphicsQueue().familyIndex);
	transferPool = CommandBuffer::CreateCommandPool(device, Backend::GetTransferQueue().familyIndex);
	computePool = CommandBuffer::CreateCommandPool(device, Backend::GetComputeQueue().familyIndex);
}

void GPUResources::UpdateAddressTableMapped() {
	std::scoped_lock lock(addressTableMutex);

	if (!gpuAddresses.IsTableDirty()) return;

	if (addressTableStagingBuffer.IsValid()) {
		addressTableStagingBuffer = BufferUtils::CreateGPUStagingBuffer(
			gpuAddresses.GetAddressTableBufferSize(),
			allocator
		);
	}

	// Copy latest address data into mapped buffer
	memcpy(addressTableStagingBuffer.m_allocInfo.pMappedData, &gpuAddresses, sizeof(BindlessBufferTable));

	ASSERT(addressTableBuffer.m_buffer != VK_NULL_HANDLE);
	ASSERT(transferPool != VK_NULL_HANDLE);

	const auto device = Backend::GetDevice();

	CommandBuffer::RecordDeferredCmd([&](VkCommandBuffer cmd) {
		VkBufferCopy copyRegion{};
		copyRegion.size = sizeof(BindlessBufferTable);
		vkCmdCopyBuffer(cmd, addressTableStagingBuffer.m_buffer, addressTableBuffer.m_buffer, 1, &copyRegion);
	}, transferPool, QueueType::Transfer, device);

	auto& tQueue = Backend::GetTransferQueue();
	lastSubmittedFence = Engine::GetState().submitCommandBuffers(tQueue);
	waitAndRecycleLastFence(lastSubmittedFence, tQueue, device);

	gpuAddresses.ClearTableDirty();
}

void GPUResources::Cleanup(VkDevice device) {
	for (uint32_t i = 0; i < static_cast<uint32_t>(BufferSlot::Count); ++i) {
		BufferSlot bufferType = static_cast<BufferSlot>(i);
		gpuAddresses.RemoveAddress(bufferType);
	}

	for (auto& [name, buf] : gpuBuffers) {
		if (buf.m_buffer != VK_NULL_HANDLE)
			BufferUtils::DestroyAllocatedBuffer(buf, allocator);
	}

	materialFlagsIDs.clear();

	if (lightListStagingBuffer.m_buffer != VK_NULL_HANDLE)
		BufferUtils::DestroyAllocatedBuffer(lightListStagingBuffer, allocator);

	if (instanceTransformsStagingBuffer.m_buffer != VK_NULL_HANDLE)
		BufferUtils::DestroyAllocatedBuffer(instanceTransformsStagingBuffer, allocator);

	if (registeredMeshes.meshIDBuffer.m_buffer != VK_NULL_HANDLE)
		BufferUtils::DestroyAllocatedBuffer(registeredMeshes.meshIDBuffer, allocator);

	if (envMapIndexBuffer.m_buffer != VK_NULL_HANDLE)
		BufferUtils::DestroyAllocatedBuffer(envMapIndexBuffer, allocator);

	if (addressTableStagingBuffer.m_buffer != VK_NULL_HANDLE)
		BufferUtils::DestroyAllocatedBuffer(addressTableStagingBuffer, allocator);

	if (addressTableBuffer.m_buffer != VK_NULL_HANDLE)
		BufferUtils::DestroyAllocatedBuffer(addressTableBuffer, allocator);

	if (graphicsPool != VK_NULL_HANDLE)
		vkDestroyCommandPool(device, graphicsPool, nullptr);

	if (transferPool != VK_NULL_HANDLE)
		vkDestroyCommandPool(device, transferPool, nullptr);

	if (computePool != VK_NULL_HANDLE)
		vkDestroyCommandPool(device, computePool, nullptr);

	if (allocator != nullptr)
		vmaDestroyAllocator(allocator);
}

void GPUResources::AddGPUBufferToGlobalAddress(BufferSlot addressBufferType, AllocatedBuffer gpuBuffer) {
	gpuBuffers[addressBufferType] = gpuBuffer;
}

void ResourceManager::InitUniformRenderTargets(
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
	//_opaque.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
	_opaque.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	_opaque.extent = drawExtent;

	ImageUtils::CreateRenderTarget(
		device,
		_opaque,
		drawImageUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator);

	// Transparent
	_transparentResolved.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	_transparentResolved.extent = drawExtent;

	ImageUtils::CreateRenderTarget(
		device,
		_transparentResolved,
		computeMinimalUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	_transparentAccumulation.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	_transparentAccumulation.extent = drawExtent;

	ImageUtils::CreateRenderTarget(
		device,
		_transparentAccumulation,
		drawImageUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	_transparentRevealage.format = VK_FORMAT_R16_SFLOAT;
	_transparentRevealage.extent = drawExtent;

	ImageUtils::CreateRenderTarget(
		device,
		_transparentRevealage,
		drawImageUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);


	// Depth resolved image
	_depthResolved.format = BASE_DEPTH_FORMAT;
	_depthResolved.extent = drawExtent;
	VkImageUsageFlags depthResolvedUsages{};
	depthResolvedUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	depthResolvedUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;
	depthResolvedUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	depthResolvedUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	ImageUtils::CreateRenderTarget(
		device,
		_depthResolved,
		depthResolvedUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator);
	_prevDepthResolved.format = BASE_DEPTH_FORMAT;
	_prevDepthResolved.extent = drawExtent;
	ImageUtils::CreateRenderTarget(
		device,
		_prevDepthResolved,
		baseComputeUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator);

	_depthRaw.format = BASE_DEPTH_FORMAT;
	_depthRaw.extent = drawExtent;
	ImageUtils::CreateRenderTarget(
		device,
		_depthRaw,
		depthResolvedUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator);

	// base ao image
	_aoRaw.format = VK_FORMAT_R8_UNORM;
	_aoRaw.extent = drawExtent;
	ImageUtils::CreateRenderTarget(
		device,
		_aoRaw,
		computeMinimalUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator);

	// ao temp
	_aoTemp.format = VK_FORMAT_R8_UNORM;
	_aoTemp.extent = drawExtent;
	ImageUtils::CreateRenderTarget(
		device,
		_aoTemp,
		computeMinimalUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator);

	// color history images
	ResetColorHistoryIndex();
	for (uint32_t i = 0; i < 2; ++i) {
		_colorHistory[i].format = VK_FORMAT_R16G16B16A16_SFLOAT;
		_colorHistory[i].extent = drawExtent;

		ImageUtils::CreateRenderTarget(
			device,
			_colorHistory[i],
			baseComputeUsages,
			VK_SAMPLE_COUNT_1_BIT,
			targetQueue,
			allocator);
	}

	// Edge image
	_aoEdgeInfo.format = VK_FORMAT_R8_UNORM;
	_aoEdgeInfo.extent = drawExtent;
	ImageUtils::CreateRenderTarget(
		device,
		_aoEdgeInfo,
		computeMinimalUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	// Velocity
	_velocity.format = VK_FORMAT_R16G16_SFLOAT;
	_velocity.extent = drawExtent;
	ImageUtils::CreateRenderTarget(
		device,
		_velocity,
		drawImageUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);
	_prevVelocity.format = VK_FORMAT_R16G16_SFLOAT;
	_prevVelocity.extent = drawExtent;
	ImageUtils::CreateRenderTarget(
		device,
		_prevVelocity,
		baseComputeUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	// View space normals
	_viewSpaceNormals.format = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	_viewSpaceNormals.extent = drawExtent;
	ImageUtils::CreateRenderTarget(
		device,
		_viewSpaceNormals,
		drawImageUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	// Tone map
	_toneMap.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	_toneMap.extent = drawExtent;
	ImageUtils::CreateRenderTarget(
		device,
		_toneMap,
		baseComputeUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	// Depth pyramid
	_hiZ.format = VK_FORMAT_R32_UINT;
	_hiZ.extent = drawExtent;
	_hiZ.mipLevelCount = HI_Z_MIP_COUNT;
	_hiZ.bPerMipStorageViews = true;
	ImageUtils::CreateRenderTarget(
		device,
		_hiZ,
		computeMinimalUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	// Linearized Min Hi Z
	_linearizedMinHiZ.format = VK_FORMAT_R32_SFLOAT;
	_linearizedMinHiZ.extent = drawExtent;
	_linearizedMinHiZ.mipLevelCount = HI_Z_MIP_COUNT;
	_linearizedMinHiZ.bPerMipStorageViews = true;
	ImageUtils::CreateRenderTarget(
		device,
		_linearizedMinHiZ,
		computeMinimalUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	// Volumetric light images
	_volumetricLight.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	_volumetricLight.extent = halfExtent;
	ImageUtils::CreateRenderTarget(
		device,
		_volumetricLight,
		computeMinimalUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);
	_volumetricBlur.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	_volumetricBlur.extent = halfExtent;
	ImageUtils::CreateRenderTarget(
		device,
		_volumetricBlur,
		computeMinimalUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);


	// Lens flare images
	_flareBright.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	_flareBright.extent = quarterExtent;
	ImageUtils::CreateRenderTarget(
		device,
		_flareBright,
		computeMinimalUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);
	_lensFlareColor.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	_lensFlareColor.extent = quarterExtent;
	ImageUtils::CreateRenderTarget(
		device,
		_lensFlareColor,
		computeMinimalUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	// AA images

	_aaColor.extent = drawExtent;
	_aaColor.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	ImageUtils::CreateRenderTarget(
		device,
		_aaColor,
		baseComputeUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	_cmaa2WorkingEdges.extent = { halfExtent.width, drawExtent.height, 1u };
	_cmaa2WorkingEdges.format = VK_FORMAT_R8_UINT;
	ImageUtils::CreateRenderTarget(
		device,
		_cmaa2WorkingEdges,
		baseComputeUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	_smaaEdges.extent = drawExtent;
	_smaaEdges.format = VK_FORMAT_R8G8_UNORM;
	ImageUtils::CreateRenderTarget(
		device,
		_smaaEdges,
		computeMinimalUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	_smaaWeights.extent = drawExtent;
	_smaaWeights.format = VK_FORMAT_R8G8B8A8_UNORM;
	ImageUtils::CreateRenderTarget(
		device,
		_smaaWeights,
		computeMinimalUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);

	_screenSpaceShadowMask.extent = drawExtent;
	_screenSpaceShadowMask.format = VK_FORMAT_R8_UNORM;
	ImageUtils::CreateRenderTarget(
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
	ImageUtils::CreateRenderTarget(
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
	ImageUtils::CreateRenderTarget(
		device,
		_postNonAAComposite,
		baseComputeUsages,
		VK_SAMPLE_COUNT_1_BIT,
		targetQueue,
		allocator
	);
}

void ResourceManager::InitShadowMapImages(
	const VkDevice device,
	DeletionQueue& queue,
	const VmaAllocator allocator)
{
	VkExtent3D csmExtent = { 4096, 4096, 1 };

	// Directional cascaded shadow map atlas
	_directionalCSMAtlas.format = BASE_DEPTH_FORMAT;
	_directionalCSMAtlas.extent = csmExtent;

	VkImageUsageFlags shadowUsages =
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
		VK_IMAGE_USAGE_SAMPLED_BIT;

	ImageUtils::CreateRenderTarget(
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

	ImageUtils::CreateRenderTarget(
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

void ResourceManager::InitRenderSamplers(
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

	// *Need to recreate due to having a m_frameSet af level
	_defaultLinearSampler = ImageUtils::createSampler(
		device,
		VK_FILTER_LINEAR,
		VK_SAMPLER_ADDRESS_MODE_REPEAT,
		VK_LOD_CLAMP_NONE,
		CURRENT_AF_LVL,
		&queue);

	_defaultNearestSampler = ImageUtils::createSampler(
		device,
		VK_FILTER_NEAREST,
		VK_SAMPLER_ADDRESS_MODE_REPEAT,
		VK_LOD_CLAMP_NONE,
		1.0f,
		&queue);

	_nearestClampSampler = ImageUtils::createSampler(
		device,
		VK_FILTER_NEAREST,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		VK_LOD_CLAMP_NONE,
		1.0f,
		&queue,
		VK_SAMPLER_MIPMAP_MODE_NEAREST
	);

	_linearClampSampler = ImageUtils::createSampler(
		device,
		VK_FILTER_LINEAR,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		VK_LOD_CLAMP_NONE,
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

EnvironmentSet ResourceManager::InitEnvironmentSetImages(
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
	env.skybox.bIsCubemap = true;
	env.skybox.bIsMipmapped = true;

	ImageUtils::CreateRenderTarget(
		device,
		env.skybox,
		usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
		samples,
		queue,
		allocator);

	// SPECULAR
	env.specular.extent = Environment::SPECULAR_EXTENTS;
	env.specular.format = environmentFormat;
	env.specular.bIsCubemap = true;
	env.specular.bPerMipStorageViews = true;
	env.specular.mipLevelCount = Environment::SPECULAR_PREFILTERED_MIP_LEVELS;

	ImageUtils::CreateRenderTarget(
		device,
		env.specular,
		usage,
		samples,
		queue,
		allocator);

	// IRRADIANCE
	env.irradiance.extent = Environment::DIFFUSE_IRRADIANCE_BASE_EXTENTS;
	env.irradiance.format = environmentFormat;
	env.irradiance.bIsCubemap = true;

	ImageUtils::CreateRenderTarget(
		device,
		env.irradiance,
		usage,
		samples,
		queue,
		allocator);

	return env;
}

void ResourceManager::InitStaticEnvironmentImages(
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


	_brdfLut.extent = Environment::LUT_IMAGE_EXTENT;
	_brdfLut.format = VK_FORMAT_R16G16_SFLOAT;

	ImageUtils::CreateRenderTarget(
		device,
		_brdfLut,
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

static glm::vec3 HsvToRgb(const float hue01, const float sat, const float val)
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

// https://github.com/GameTechDev/XeGTAO/blob/a5b1686c7ea37788eeb3576b5be47f7c03db532c/Source/Rendering/Shaders/XeGTAO.h#L120
static const uint32_t HILBERT_LEVEL = 6u;
static const uint32_t HILBERT_WIDTH = 1u << HILBERT_LEVEL;
static const uint32_t HILBERT_AREA = HILBERT_WIDTH * HILBERT_WIDTH;

static uint32_t HilbertIndex(uint32_t posX, uint32_t posY) {
	uint32_t index = 0u;
	for (uint32_t curLevel = HILBERT_WIDTH / 2u; curLevel > 0u; curLevel /= 2u) {
		uint32_t regionX = (posX & curLevel) > 0u;
		uint32_t regionY = (posY & curLevel) > 0u;
		index += curLevel * curLevel * ( (3u * regionX) ^ regionY);
		if (regionY == 0u) {
			if (regionX == 1u) {
				posX = static_cast<uint32_t>(HILBERT_WIDTH - 1u) - posX;
				posY = static_cast<uint32_t>(HILBERT_WIDTH - 1u) - posY;
			}
		}
		uint32_t temp = posX;
		posX = posY;
		posY = temp;
	}
	return index;
}

void ResourceManager::InitTextures(
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

	_normalTexture.extent = texExtent;
	_normalTexture.format = VK_FORMAT_R8G8B8A8_UNORM;
	_normalTexture.bIsMipmapped = true;

	uint32_t flatNormal = glm::packUnorm4x8(glm::vec4(0.5f, 0.5f, 1.0f, 1.0f)); // X = 128, Y = 128, Z = 255, A = 255
	ImageUtils::CreateTexture(
		device,
		cmdPool,
		(void*)&flatNormal,
		_normalTexture,
		usage,
		samples,
		imageQueue,
		bufferQueue,
		allocator);


	_emissiveTexture.extent = texExtent;
	_emissiveTexture.format = format;
	_emissiveTexture.bIsMipmapped = true;

	uint32_t blackEmissive = glm::packUnorm4x8(glm::vec4(0, 0, 0, 1)); // No emission
	ImageUtils::CreateTexture(
		device,
		cmdPool,
		(void*)&blackEmissive,
		_emissiveTexture,
		usage,
		samples,
		imageQueue,
		bufferQueue,
		allocator);

	_metalRoughTexture.extent = texExtent;
	_metalRoughTexture.format = VK_FORMAT_R8G8B8A8_UNORM;
	_metalRoughTexture.bIsMipmapped = true;

	// From what I've read about modern GLTF pbr, g is roughness and b is metallic.
	uint8_t mrPixelData[4] {
		static_cast<uint8_t>(0.0f * 255), // metallic?
		static_cast<uint8_t>(0.5f * 255), // roughness
		static_cast<uint8_t>(0.0f * 255), // metallic?
		static_cast<uint8_t>(1.0f * 255)
	};
	ImageUtils::CreateTexture(
		device,
		cmdPool,
		(void*)&mrPixelData,
		_metalRoughTexture,
		usage,
		samples,
		imageQueue,
		bufferQueue,
		allocator);


	_dummyTexture.extent = texExtent;
	_dummyTexture.format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;
	//_dummyTexture.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	glm::vec4 black = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
	ImageUtils::CreateTexture(
		device,
		cmdPool,
		(void*)&black,
		_dummyTexture,
		usage,
		VK_SAMPLE_COUNT_1_BIT,
		imageQueue,
		bufferQueue,
		allocator
	);

	_whiteTexture.extent = texExtent;
	_whiteTexture.format = format;
	_whiteTexture.bIsMipmapped = true;

	uint32_t white = glm::packUnorm4x8(glm::vec4(1, 1, 1, 1));
	ImageUtils::CreateTexture(
		device,
		cmdPool,
		(void*)&white,
		_whiteTexture,
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

	_errorCheckerboardTexture.extent = checkerboardedImageExtent;
	_errorCheckerboardTexture.format = format;
	_errorCheckerboardTexture.bIsMipmapped = true;
	ImageUtils::CreateTexture(
		device,
		cmdPool,
		pixels.data(),
		_errorCheckerboardTexture,
		usage,
		samples,
		imageQueue,
		bufferQueue,
		allocator);

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

		glm::vec3 rgb = HsvToRgb(hue, 0.95f, 1.0f);
		rgb.g *= 0.9f;

		const glm::vec4 rgba(rgb, 1.0);
		lutPixels[static_cast<size_t>(x)] = glm::packUnorm4x8(rgba);
	}


	_rainbowLUTTexture.format = VK_FORMAT_R8G8B8A8_UNORM;
	_rainbowLUTTexture.extent = { lutWidth, lutHeight, 1u };

	ImageUtils::CreateTexture(
		device,
		cmdPool,
		lutPixels.data(),
		_rainbowLUTTexture,
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
	_cookieGoboTexture.extent = { 512, 512, 1 };
	_cookieGoboTexture.format = VK_FORMAT_R8_UNORM;

	ImageUtils::CreateTexture(
		device,
		cmdPool,
		cookieData,
		_cookieGoboTexture,
		usage,
		samples,
		imageQueue,
		bufferQueue,
		allocator
	);

	stbi_image_free(cookieData);


	_areaSMAATexture.extent = { AREATEX_WIDTH, AREATEX_HEIGHT, 1 };
	_areaSMAATexture.format = VK_FORMAT_R8G8_UNORM;

	ImageUtils::CreateTexture(
		device,
		cmdPool,
		areaTexBytes,
		_areaSMAATexture,
		usage,
		samples,
		imageQueue,
		bufferQueue,
		allocator
	);

	_searchSMAATexture.extent = { SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 1 };
	_searchSMAATexture.format = VK_FORMAT_R8_UNORM;

	ImageUtils::CreateTexture(
		device,
		cmdPool,
		searchTexBytes,
		_searchSMAATexture,
		usage,
		samples,
		imageQueue,
		bufferQueue,
		allocator
	);

	// Hilbert curve lut
	std::vector<uint16_t> hcData;
	hcData.resize(64 * 64);
	for (int x = 0; x < 64; x++) {
		for (int y = 0; y < 64; y++) {
			uint32_t r2index = HilbertIndex(x, y);
			ASSERT(r2index < 65536);
			hcData[static_cast<size_t>(x + 64 * y)] = static_cast<uint16_t>(r2index);
		}
	}

	_hilbertCurveLUT.extent = { 64u, 64u, 1u };
	_hilbertCurveLUT.format = VK_FORMAT_R16_UINT;

	ImageUtils::CreateTexture(
		device,
		cmdPool,
		hcData.data(),
		_hilbertCurveLUT,
		usage,
		samples,
		imageQueue,
		bufferQueue,
		allocator
	);

	_dummyUint8Texture.format = VK_FORMAT_R8_UINT;
	_dummyUint8Texture.extent = texExtent;
	ImageUtils::CreateRenderTarget(
		device,
		_dummyUint8Texture,
		usage,
		samples,
		imageQueue,
		allocator
	);
}
