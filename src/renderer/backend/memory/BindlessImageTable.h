#pragma once

#include <renderer/backend/VulkanForward.h>
#include "AllocatedImage.h"
#include <renderer/RendererDefinitions.h>
#include "../../../core/asset/AssetUploadTypes.h"
#include <span>

#define COLOR_RESOLVED_A RD::Renderer_RenderTarget::ColorHistoryA
#define COLOR_RESOLVED_B RD::Renderer_RenderTarget::ColorHistoryB

#define RADIANCE_RESOLVED_A RD::Renderer_RenderTarget::DiffuseRadianceA
#define RADIANCE_RESOLVED_B RD::Renderer_RenderTarget::DiffuseRadianceB

#define GI_RESOLVED_A RD::Renderer_RenderTarget::GIHistoryA
#define GI_RESOLVED_B RD::Renderer_RenderTarget::GIHistoryB

#define VOL_LIGHT_RESOLVED_A RD::Renderer_RenderTarget::VolLightHistoryA
#define VOL_LIGHT_RESOLVED_B RD::Renderer_RenderTarget::VolLightHistoryB

#define FROXEL_SCATTER_RESOLVED_A RD::Renderer_RenderTarget::FroxelScatterExtA
#define FROXEL_SCATTER_RESOLVED_B RD::Renderer_RenderTarget::FroxelScatterExtB

class Allocator;
class StagingBuffer;
struct Extents3D;

namespace RD = RendererDefinitions;

class BindlessImageTable final
{
public:
	void Init(
		Extents3D drawExtent,
		uint32_t environmentSetCount,
		RD::ShadowQuality shadowQuality,
		VkDevice device,
		Allocator& allocator);
	void Shutdown(VkDevice device, Allocator& allocator);

	void PreallocateEquirects(std::span<const char* const> hdrPaths, Allocator& allocator);
	void UploadStaticTextures(StagingBuffer& staging, VkCommandBuffer cmd);
	void UploadEquirects(
		std::span<const char* const> hdrPaths,
		Allocator&                   allocator,
		VkCommandBuffer              cmd);
	void FreeEquirects(Allocator& allocator);

	void UpdateRenderTargets(Extents3D drawExtent, Allocator& allocator);

	// --- Render targets ---
	const AllocatedImage& GetRenderTarget(RD::Renderer_RenderTarget slot) const;
	std::array<AllocatedImage, RD::RENDER_TARGET_COUNT>& GetRenderTargetsMutable() { return m_renderTargets; }
	const std::array<AllocatedImage, RD::RENDER_TARGET_COUNT>& GetRenderTargets()  const { return m_renderTargets; }
	void TransitionRenderTargetsFromUndefined(VkCommandBuffer cmd);

	// Quick resize for resolution swap
	void UpdateCSMAtlasExtent(RD::ShadowQuality quality, Allocator& allocator);

	// When rt shadows enable free up vram
	// Cached info
	void FreeCSMAtlas(Allocator& allocator);
	void RecreateCSMAtlas(Allocator& allocator);

	// --- Samplers ---
	VkSampler GetSampler(RD::Renderer_Sampler slot) const;
	const std::array<VkSampler, RD::SAMPLER_COUNT>& GetSamplers() const { return m_samplers; }

	// --- Static textures ---
	const AllocatedImage& GetStaticTexture(RD::Renderer_Texture slot) const;
	const std::array<AllocatedImage, RD::STATIC_TEXTURE_COUNT>& GetStaticTextures() const { return m_staticTextures; }

	size_t CalcStaticTexturesStagingSize() const;

	// --- Environment sets ---
	const EnvironmentSet&  GetEnvironmentSet(uint32_t index)        const;
	EnvironmentSet&        GetEnvironmentSetMutable(uint32_t index);
	uint32_t               EnvironmentSetCount()                    const noexcept;

	// --- Asset textures ---
	uint32_t              PushAssetTexture(AllocatedImage image);
	const AllocatedImage& GetAssetTexture(uint32_t index)           const;
	AllocatedImage&       GetAssetTextureMutable(uint32_t index);
	void                  FreeAssetTexture(uint32_t index);
	uint32_t              AssetTextureCount()                 const noexcept { return static_cast<uint32_t>(m_assetTextures.size()); }
	bool                  IsAssetTextureValid(uint32_t index) const noexcept;

	uint32_t ResolveAssetSampler(const SamplerDesc& desc, VkDevice device);

	VkSampler ResolveDefaultAssetSampler(const AllocatedImage& img) const noexcept;

	std::vector<uint32_t> UploadAssetTextures(
		SceneUploadBatch& batch,
		VkDevice          device,
		Allocator&        allocator,
		StagingBuffer&    staging,
		VkCommandBuffer   cmd);

	std::span<const AllocatedImage> GetAssetTextureSpan()   const noexcept { return m_assetTextures; }
	std::span<const EnvironmentSet> GetEnvironmentSetSpan() const noexcept { return m_environmentSets; }
	std::span<const AllocatedImage> GetStaticTextureSpan()  const noexcept { return m_staticTextures; }
	std::span<const AllocatedImage> GetRenderTargetSpan()   const noexcept { return m_renderTargets; }

	// --- Descriptor arrays ---
	uint32_t PushCombined(VkImageView view, VkSampler sampler);
	void     PushCombinedBatch(std::span<AllocatedImage> images, VkSampler sampler);
	uint32_t PushSamplerCube(VkImageView view, VkSampler sampler);

	void RegisterShadowMapsAsCombined(VkSampler shadowSampler);
	void RegisterStaticTexturesAsCombined(VkSampler genericSampler);
	void RegisterEnvironmentSetAsCube(uint32_t envSetIndex, VkSampler skyboxSampler,
									  VkSampler specularSampler, VkSampler irradianceSampler);

	const std::vector<VkDescriptorImageInfo>& GetCombinedSamplerArray() const noexcept { return m_combinedViews; }
	const std::vector<VkDescriptorImageInfo>& GetSamplerCubeArray()     const noexcept { return m_samplerCubeViews; }
	uint32_t CombinedSamplerCount() const noexcept { return static_cast<uint32_t>(m_combinedViews.size()); }
	uint32_t SamplerCubeCount()     const noexcept { return static_cast<uint32_t>(m_samplerCubeViews.size()); }

	void ClearDescriptorArrays()
	{
		std::scoped_lock l(m_combinedMutex, m_samplerCubeMutex);
		m_combinedViews.clear();
		m_combinedViewHashToID.clear();
		m_samplerCubeViews.clear();
		m_samplerCubeViewHashToID.clear();
	}

	void BuildInitialCombinedSamplerArray();
	void BuildInitialSamplerCubeArray();

	void MarkDirty() noexcept { m_bIsTableDirty = true; ++m_cpuVersion; }
	bool IsTableDirty() const noexcept { return m_bIsTableDirty; }
	void ClearDirty() noexcept { m_bIsTableDirty = false; }

	bool IsShadowAtlasCached() const noexcept { return m_cachedCsmAtlasInfo.isActive; }
	uint32_t GetCachedCSMRes() const noexcept { return m_cachedCsmAtlasInfo.width; }
private:
	void CreateRenderTargets(Extents3D drawExtent, Allocator& allocator);
	void CreateStaticTextures(Allocator& allocator);
	void CreateEnvironmentSets(uint32_t setCount, Allocator& allocator);
	void CreateShadowMaps(RD::ShadowQuality quality, Allocator& allocator);
	void CreateFroxelFogTargets(Allocator& allocator);
	void CreateSamplers(VkDevice device);

	void FreeRenderTargets(Allocator& allocator);
	void FreeShadowMaps(Allocator& allocator);
	void FreeFroxelFogTargets(Allocator& allocator);
	void FreeStaticTextures(Allocator& allocator);
	void FreeEnvironmentSets(Allocator& allocator);
	void FreeSamplers(VkDevice device);

	void SetRenderTarget(RD::Renderer_RenderTarget slot, AllocatedImage image);
	void SetStaticTexture(RD::Renderer_Texture slot, AllocatedImage image);
	void SetSampler(RD::Renderer_Sampler slot, VkSampler sampler);
	void AddEnvironmentSet(EnvironmentSet envSet);

	uint32_t PushCombinedLocked(VkImageView view, VkSampler sampler);
	uint32_t PushSamplerCubeLocked(VkImageView view, VkSampler sampler);
	void UpdateCombinedLocked(uint32_t index, VkImageView view, VkSampler sampler);

	std::array<AllocatedImage, RD::RENDER_TARGET_COUNT>                         m_renderTargets{};
	std::array<AllocatedImage, RD::STATIC_TEXTURE_COUNT>                        m_staticTextures{};
	std::array<EnvironmentSet, static_cast<size_t>(RD::MAX_ENVIRONMENT_SETS)>   m_environmentSets{};
	std::array<VkSampler,      RD::SAMPLER_COUNT>                               m_samplers{};
	std::vector<AllocatedImage>                                                 m_assetTextures{};
	std::vector<VkSampler>                                                      m_assetSamplers{};
	std::vector<SamplerDesc>                                                    m_assetSamplerDescs{};
	std::unordered_map<std::string, AssetTextureEntry>                          m_assetTextureCache{};
	std::mutex                                                                  m_assetTextureCacheMutex{};

	bool     m_bAreShadowsCreated   = false;
	bool     m_bAreFroxelFogCreated = false;
	bool     m_bIsTableDirty      = false;
	uint32_t m_cpuVersion         = 1u;
	uint32_t m_gpuVersion         = 0u;

	struct CachedCSMAtlasInfo
	{
		uint32_t csmAtlasBindlessID = UINT32_MAX;
		uint32_t width = 0;
		bool isActive = false;
	} m_cachedCsmAtlasInfo{};

	using ImageViewSamplerKey = std::pair<VkImageView, VkSampler>;

	struct HashPair
	{
		size_t operator()(const ImageViewSamplerKey& k) const noexcept
		{
			return std::hash<uintptr_t>()(reinterpret_cast<uintptr_t>(k.first))
				 ^ (std::hash<uintptr_t>()(reinterpret_cast<uintptr_t>(k.second)) << 1);
		}
	};
	struct EqualPair
	{
		bool operator()(const ImageViewSamplerKey& a, const ImageViewSamplerKey& b) const noexcept
		{ return a.first == b.first && a.second == b.second; }
	};

	std::mutex m_combinedMutex;
	std::mutex m_samplerCubeMutex;

	std::vector<VkDescriptorImageInfo>                                         m_combinedViews;
	std::unordered_map<ImageViewSamplerKey, uint32_t, HashPair, EqualPair>     m_combinedViewHashToID;

	std::vector<VkDescriptorImageInfo>                                         m_samplerCubeViews;
	std::unordered_map<ImageViewSamplerKey, uint32_t, HashPair, EqualPair>     m_samplerCubeViewHashToID;

	uint32_t m_shadowMapCombinedEnd     = 0u;
	uint32_t m_staticTextureCombinedEnd = 0u;
};

namespace TemporalHistory
{
	struct Slots
	{
		RD::Renderer_RenderTarget read;   // previous frame's output (history to accumulate against)
		RD::Renderer_RenderTarget write;  // this frame's output (also what later passes sample)
	};

	inline Slots GetColorHistorySlots(uint64_t frameIndex)
	{
		const bool odd = (frameIndex & 1ull) != 0ull;
		return odd
			? Slots{ RD::Renderer_RenderTarget::ColorHistoryB,
					 RD::Renderer_RenderTarget::ColorHistoryA }
			: Slots{ RD::Renderer_RenderTarget::ColorHistoryA,
					 RD::Renderer_RenderTarget::ColorHistoryB };
	}

	inline Slots GetDiffuseRadianceSlots(uint64_t frameIndex)
	{
		const bool odd = (frameIndex & 1ull) != 0ull;
		return odd
			? Slots{ RD::Renderer_RenderTarget::DiffuseRadianceB,
					 RD::Renderer_RenderTarget::DiffuseRadianceA }
			: Slots{ RD::Renderer_RenderTarget::DiffuseRadianceA,
					 RD::Renderer_RenderTarget::DiffuseRadianceB };
	}

	inline Slots GetGIHistorySlots(uint64_t frameIndex)
	{
		const bool odd = (frameIndex & 1ull) != 0ull;
		return odd
			? Slots{ RD::Renderer_RenderTarget::GIHistoryB,
					 RD::Renderer_RenderTarget::GIHistoryA }
			: Slots{ RD::Renderer_RenderTarget::GIHistoryA,
					 RD::Renderer_RenderTarget::GIHistoryB };
	}

	inline Slots GetVolLightHistorySlots(uint64_t frameIndex)
	{
		const bool odd = (frameIndex & 1ull) != 0ull;
		return odd
			? Slots{ RD::Renderer_RenderTarget::VolLightHistoryB,
					 RD::Renderer_RenderTarget::VolLightHistoryA }
			: Slots{ RD::Renderer_RenderTarget::VolLightHistoryA,
					 RD::Renderer_RenderTarget::VolLightHistoryB };
	}

	inline Slots GetFroxelScatterSlots(uint64_t frameIndex)
	{
		const bool odd = (frameIndex & 1ull) != 0ull;
		return odd
			? Slots{ RD::Renderer_RenderTarget::FroxelScatterExtB,
					 RD::Renderer_RenderTarget::FroxelScatterExtA }
			: Slots{ RD::Renderer_RenderTarget::FroxelScatterExtA,
					 RD::Renderer_RenderTarget::FroxelScatterExtB };
	}
}
