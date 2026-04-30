#pragma once

#include "common/EngineTypes.h"
#include "common/ResourceTypes.h"
#include "common/EngineConstants.h"

// Anisotropic Filtering
inline float CURRENT_AF_LVL = ANISOTROPY_LEVEL_8;

struct GPUResources {
public:
	VmaAllocator& GetAllocator() { return allocator; }
	DeletionQueue& GetMainDQueue() { return mainDeletionQueue; }
	DeletionQueue& GetTempDQueue() { return tempDeletionQueue; }
	DeletionQueue& GetRenderTargetDQueue() { return renderTargetQueue; }
	DeletionQueue& GetDynamicPipelineQueue() { return dynamicPipelineQueue; }
	DeletionQueue& GetDynamicPipelineShaderStagesQueue() { return dynamicPipelineShaderStagesQueue; }
	VkCommandPool& GetGraphicsPool() { return graphicsPool; }
	VkCommandPool& GetTransferPool() { return transferPool; }
	VkCommandPool& GetComputePool() { return computePool; }
	VkFence& GetLastSubmittedFence() { return lastSubmittedFence; }

	std::vector<uint32_t>& GetMaterialFlagsByID() { return materialFlagsIDs; }

	void Init(const VkDevice device);

	BindlessBufferTable& GetAddressTable() { return gpuAddresses; }
	AllocatedBuffer& GetAddressTableBuffer() { return addressTableBuffer; }

	AllocatedBuffer& GetGPUAddrsBuffer(BufferSlot type) { return gpuBuffers.at(type); }
	bool ContainsGPUBuffer(BufferSlot type) const {
		auto it = gpuBuffers.find(type);
		return it != gpuBuffers.end() && it->second.m_buffer != VK_NULL_HANDLE;
	}
	void AddGPUBufferToGlobalAddress(BufferSlot addressBufferType, AllocatedBuffer gpuBuffer);
	void ClearAddressBuffer(BufferSlot type) { gpuBuffers.erase(type); }

	// Marked dirty whenever new addresses are added and clean when this function finishes its upload.
	// This is designed for the global address table only.
	void UpdateAddressTableMapped();

	// All submesh access
	// Maps meshes to their vertex/index buffer regions for indirect drawing
	MeshRegistry& GetResgisteredMeshes() { return registeredMeshes; }

	ImageLUTManager& GetLUTManager() { return lutManager; }

	void AddImageLUTEntry(const ImageLUTEntry& entry) {
		GetLUTManager().AddEntry(entry);
	}

	void ClearLUTEntries() {
		GetLUTManager().Clear();
	}

	AllocatedBuffer& GetLightListStagingBuffer() {
		return lightListStagingBuffer;
	}

	AllocatedBuffer& GetInstanceTransformsStagingBuffer() {
		return instanceTransformsStagingBuffer;
	}

	TotalAssetDataCounts modelDataCounts;

	// Uniform buffers
	AllocatedBuffer envMapIndexBuffer;

	void Cleanup(VkDevice device);

	bool assetsLoaded = false;

	// stores search and area lut texture ids
	BindlessAccessPush smaaTextures;

private:
	BindlessBufferTable gpuAddresses{};
	AllocatedBuffer addressTableBuffer; // descriptor written buffer, mapped from gpuaddresses
	AllocatedBuffer addressTableStagingBuffer;
	mutable std::mutex addressTableMutex;

	AllocatedBuffer lightListStagingBuffer;

	AllocatedBuffer instanceTransformsStagingBuffer;

	MeshRegistry registeredMeshes;

	ImageLUTManager lutManager{};

	std::vector<uint32_t> materialFlagsIDs;

	std::unordered_map<BufferSlot, AllocatedBuffer> gpuBuffers{};

	VmaAllocator allocator = nullptr;
	DeletionQueue mainDeletionQueue;                // Runtime static
	DeletionQueue tempDeletionQueue;                // Primary use in asset loading prep
	DeletionQueue renderTargetQueue;                // For when new extents occur

	DeletionQueue dynamicPipelineShaderStagesQueue;
	DeletionQueue dynamicPipelineQueue;

	// Graphics work
	VkCommandPool graphicsPool = VK_NULL_HANDLE;
	VkCommandPool transferPool = VK_NULL_HANDLE;
	VkCommandPool computePool = VK_NULL_HANDLE;
	VkFence lastSubmittedFence = VK_NULL_HANDLE;
};

namespace ResourceManager {
	extern ImageTableManager _globalImageManager;
	extern EnvironmentSet _environmentSets[MAX_ENV_SETS];
	extern EnvironmentIndexArray _envMapIdxArray;

	extern glm::vec4 _luminanceSums[MAX_LUMINANCE_GROUPS];

	AllocatedImage& GetOpaque_Target();
	AllocatedImage& GetTransparentResolved_Target();
	// For OIT
	AllocatedImage& GetTransparentAccumulation_Target();
	AllocatedImage& GetTransparentRevealage_Target();

	AllocatedImage& GetToneMap_Target();

	// Pre pass depth
	AllocatedImage& GetDepthResolved_Target();
	AllocatedImage& GetPrevDepthResolved_Target();
	AllocatedImage& GetHiZ_Target(); // r32uint packed min/max
	AllocatedImage& GetLinearizedMinHiZ_Target(); // r32f linearized min

	// An empty base depth
	AllocatedImage& GetDepthRaw_Target();

	AllocatedImage& GetAORaw_Target();
	AllocatedImage& GetAOTemp_Target();

	AllocatedImage& GetColorHistoryRead_Target();
	AllocatedImage& GetColorHistoryWrite_Target();
	void FlipColorHistory();
	void ResetColorHistoryIndex();

	AllocatedImage& GetFlareBright_Target();
	AllocatedImage& GetLensFlareColor_Target();
	AllocatedImage& GetVelocity_Target();
	AllocatedImage& GetPrevVelocity_Target();
	AllocatedImage& GetViewSpaceNormals_Target();
	AllocatedImage& GetVolumetricLight_Target();
	AllocatedImage& GetVolumetricBlur_Target();
	AllocatedImage& GetDirectionalCSMAtlas_Target();
	AllocatedImage& GetScreenSpaceShadowMask_Target();
	AllocatedImage& GetBentNormals_Target();
	AllocatedImage& GetAOEdgeInfo_Target();
	AllocatedImage& GetAAColor_Target();
	AllocatedImage& GetPostNonAAComposite_Target();
	AllocatedImage& GetCMAA2WorkingEdges_Target();
	AllocatedImage& GetSMAAEdges_Target();
	AllocatedImage& GetSMAAWeights_Target();
	AllocatedImage& GetFlashlightShadowMap_Target();

	AllocatedImage& GetRainbowLUT_Texture();
	AllocatedImage& GetCookieGobo_Texture();
	AllocatedImage& GetAreaSMAA_Texture();
	AllocatedImage& GetSearchSMAA_Texture();

	const VkSampler GetNearestClamp_Sampler();
	const VkSampler GetLinearClamp_Sampler();
	const VkSampler GetHiZ_Sampler();
	const VkSampler GetLinearLODClamp_Sampler();
	const VkSampler GetPointBorder_Sampler();
	const VkSampler GetTaaHistory_Sampler();
	const VkSampler GetNoise_Sampler();
	const VkSampler GetShadowMap_Sampler();

	// Empty black image
	AllocatedImage& GetDummy_Texture();

	void InitRenderSamplers(
		const VkDevice device,
		DeletionQueue& queue);

	void InitShadowMapImages(
		const VkDevice device,
		DeletionQueue& queue,
		const VmaAllocator allocator);

	void InitUniformRenderTargets(
		const VkDevice device,
		DeletionQueue& targetQueue,
		const VmaAllocator allocator,
		const VkExtent3D drawExtent);

	AllocatedImage& GetMetalRough_Texture();
	AllocatedImage& GetWhiteMat_Texture();
	AllocatedImage& GetEmissive_Texture();
	AllocatedImage& GetNormal_Texture();
	AllocatedImage& GetCheckboard_Texture();
	AllocatedImage& GetHilbertCurveLUT_Texture();
	AllocatedImage& GetDummyUint8_Texture();
	const VkSampler GetDefaultLinear_Sampler();
	const VkSampler GetDefaultNearest_Sampler();
	void InitTextures(
		const VkDevice device,
		VkCommandPool cmdPool,
		DeletionQueue& imageQueue,
		DeletionQueue& bufferQueue,
		const VmaAllocator allocator);

	AllocatedImage& GetBRDF_Texture();
	const VkSampler GetBRDF_Sampler();
	const VkSampler GetSpecularPrefilter_Sampler();
	const VkSampler GetIrradiance_Sampler();
	const VkSampler GetSkyBox_Sampler();

	EnvironmentSet InitEnvironmentSetImages(
		const VkDevice device,
		DeletionQueue& queue,
		const VmaAllocator allocator);

	// Defines samplers and the brdf
	void InitStaticEnvironmentImages(
		const VkDevice device,
		DeletionQueue& queue,
		const VmaAllocator allocator);
}
