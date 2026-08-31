#include "pch.h"

#include "BindlessImageTable.h"
#include "ImageDescs.h"
#include "ResourceAllocator.h"
#include "../ImageUtils.h"
#include "Staging.h"
#include "TextureStaging.h"

namespace RTDescs  = RenderTargetDescs;
namespace EnvDescs = EnvironmentMapDescs;
namespace STDescs  = StaticTextureDescs;

static size_t Index(RD::Renderer_RenderTarget slot) noexcept
{
	ASSERT(static_cast<size_t>(slot) < RD::RENDER_TARGET_COUNT);
	return static_cast<size_t>(slot);
}
static size_t Index(RD::Renderer_Texture slot) noexcept
{
	ASSERT(static_cast<size_t>(slot) < RD::STATIC_TEXTURE_COUNT);
	return static_cast<size_t>(slot);
}
static size_t Index(RD::Renderer_Sampler slot) noexcept
{
	ASSERT(static_cast<size_t>(slot) < RD::SAMPLER_COUNT);
	return static_cast<size_t>(slot);
}

static Vulkan_Format ResolveAssetFormat(const TextureDesc& desc)
{
	switch (desc.format)
	{
	case TextureFormat::BC7:
		return desc.isSRGB ? Vulkan_Format::BC7srgb : Vulkan_Format::BC7unorm;
	case TextureFormat::BC5:
		return Vulkan_Format::BC5unorm;
	default:
		return desc.isSRGB ? Vulkan_Format::RGBA8srgb : Vulkan_Format::RGBA8unorm;
	}
}

static uint32_t ClampMipCount(Extents3D extent, uint32_t requested)
{
	uint32_t maxDim = std::max(extent.Width(), extent.Height());
	uint32_t mips = 1u;
	while (maxDim > 1u) { maxDim >>= 1; ++mips; }
	return std::min(requested, mips);
}

// --------
// Hilbert
// --------

static constexpr uint32_t HILBERT_LEVEL = 6u;
static constexpr uint32_t HILBERT_WIDTH = 1u << HILBERT_LEVEL;

static uint32_t HilbertIndex(uint32_t posX, uint32_t posY)
{
	uint32_t index = 0u;
	for (uint32_t curLevel = HILBERT_WIDTH / 2u; curLevel > 0u; curLevel /= 2u)
	{
		uint32_t regionX = (posX & curLevel) > 0u;
		uint32_t regionY = (posY & curLevel) > 0u;
		index += curLevel * curLevel * ((3u * regionX) ^ regionY);
		if (regionY == 0u)
		{
			if (regionX == 1u)
			{
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

// ---------
// Rainbow
// ---------

static glm::vec3 HsvToRgb(float hue01, float sat, float val)
{
	const float hue = hue01 - std::floor(hue01);
	const float c   = val * sat;
	const float h6  = hue * 6.0f;
	const float x   = c * (1.0f - std::fabsf(std::fmodf(h6, 2.0f) - 1.0f));
	const float m   = val - c;

	glm::vec3 rgb(0.0f);
	if      (h6 < 1.0f) rgb = { c, x, 0 };
	else if (h6 < 2.0f) rgb = { x, c, 0 };
	else if (h6 < 3.0f) rgb = { 0, c, x };
	else if (h6 < 4.0f) rgb = { 0, x, c };
	else if (h6 < 5.0f) rgb = { x, 0, c };
	else                rgb = { c, 0, x };
	return rgb + glm::vec3(m);
}


// ======
// INIT
// ======

void BindlessImageTable::Init(
	Extents3D drawExtent,
	uint32_t  environmentSetCount,
	RD::ShadowQuality shadowQuality,
	VkDevice  device,
	Allocator& allocator)
{
	CreateSamplers(device);
	CreateRenderTargets(drawExtent, allocator);
	CreateShadowMaps(shadowQuality, allocator);
	CreateFroxelFogTargets(allocator);
	CreateStaticTextures(allocator);
	CreateEnvironmentSets(environmentSetCount, allocator);
}

void BindlessImageTable::Shutdown(VkDevice device, Allocator& allocator)
{
	ClearDescriptorArrays();
	FreeRenderTargets(allocator);
	FreeShadowMaps(allocator);
	FreeFroxelFogTargets(allocator);
	FreeStaticTextures(allocator);
	FreeEnvironmentSets(allocator);
	FreeSamplers(device);
	m_assetTextures.clear();
}

// ===============
// RENDER TARGETS
// ===============

void BindlessImageTable::CreateRenderTargets(Extents3D drawExtent, Allocator& allocator)
{
	const Extents3D half = {
		(drawExtent.Width() + 1) / 2,
		(drawExtent.Height() + 1) / 2,
		1
	};
	const Extents3D quarter = {
		(drawExtent.Width() + 3) / 4,
		(drawExtent.Height() + 3) / 4,
		1
	};

	SetRenderTarget(RD::Renderer_RenderTarget::Opaque,                      allocator.AllocateImage(RTDescs::Opaque(drawExtent)));
	SetRenderTarget(RD::Renderer_RenderTarget::HDRScene,                    allocator.AllocateImage(RTDescs::HDRScene(drawExtent)));
	SetRenderTarget(RD::Renderer_RenderTarget::TransparentAccumulation,     allocator.AllocateImage(RTDescs::TransparentAccumulation(drawExtent)));
	SetRenderTarget(RD::Renderer_RenderTarget::TransparentVelocityAccum,    allocator.AllocateImage(RTDescs::TransparentVelocityAccum(drawExtent)));
	SetRenderTarget(RD::Renderer_RenderTarget::TransparentRevealage,        allocator.AllocateImage(RTDescs::TransparentRevealage(drawExtent)));
	SetRenderTarget(RD::Renderer_RenderTarget::DepthResolved,               allocator.AllocateImage(RTDescs::DepthResolved(drawExtent)));
	SetRenderTarget(RD::Renderer_RenderTarget::PrevDepthResolved,           allocator.AllocateImage(RTDescs::PrevDepth(drawExtent)));
	SetRenderTarget(RD::Renderer_RenderTarget::Velocity,                    allocator.AllocateImage(RTDescs::Velocity(drawExtent)));
	SetRenderTarget(RD::Renderer_RenderTarget::PrevVelocity,                allocator.AllocateImage(RTDescs::PrevVelocity(drawExtent)));
	SetRenderTarget(RD::Renderer_RenderTarget::BloomMipchain,               allocator.AllocateImage(RTDescs::BloomMipchain(half)));
	SetRenderTarget(RD::Renderer_RenderTarget::ViewNormals,                 allocator.AllocateImage(RTDescs::ViewNormals(drawExtent)));
	SetRenderTarget(RD::Renderer_RenderTarget::PrevViewNormals,             allocator.AllocateImage(RTDescs::PrevViewNormals(drawExtent)));
	SetRenderTarget(RD::Renderer_RenderTarget::Visibility,                  allocator.AllocateImage(RTDescs::Visibility(drawExtent)));
	SetRenderTarget(RD::Renderer_RenderTarget::Tonemap,                     allocator.AllocateImage(RTDescs::ToneMap(drawExtent)));
	SetRenderTarget(RD::Renderer_RenderTarget::AoEdgeInfo,                  allocator.AllocateImage(RTDescs::AOEdgeInfo(half)));
	SetRenderTarget(RD::Renderer_RenderTarget::AORaw,                       allocator.AllocateImage(RTDescs::AORaw(half)));
	SetRenderTarget(RD::Renderer_RenderTarget::AOTemp,                      allocator.AllocateImage(RTDescs::AOTemp(half)));
	SetRenderTarget(RD::Renderer_RenderTarget::BentNormalAO,                allocator.AllocateImage(RTDescs::BentNormalAO(drawExtent)));
	SetRenderTarget(RD::Renderer_RenderTarget::BentNormalAOHalf,            allocator.AllocateImage(RTDescs::BentNormalAOHalf(half)));
	SetRenderTarget(RD::Renderer_RenderTarget::ColorHistoryA,               allocator.AllocateImage(RTDescs::ColorHistory(drawExtent, 0)));
	SetRenderTarget(RD::Renderer_RenderTarget::ColorHistoryB,               allocator.AllocateImage(RTDescs::ColorHistory(drawExtent, 1)));
	SetRenderTarget(RD::Renderer_RenderTarget::PostNonAAComposite,          allocator.AllocateImage(RTDescs::PostNonAAComposite(drawExtent)));
	SetRenderTarget(RD::Renderer_RenderTarget::SSContactShadows,            allocator.AllocateImage(RTDescs::ScreenSpaceShadowMask(drawExtent)));
	SetRenderTarget(RD::Renderer_RenderTarget::VolumetricLight,             allocator.AllocateImage(RTDescs::VolumetricLight(half)));
	SetRenderTarget(RD::Renderer_RenderTarget::VolumetricLightBlur,         allocator.AllocateImage(RTDescs::VolumetricBlur(half)));
	SetRenderTarget(RD::Renderer_RenderTarget::VolLightHistoryA,            allocator.AllocateImage(RTDescs::VolLightHistory(half, 0)));
	SetRenderTarget(RD::Renderer_RenderTarget::VolLightHistoryB,            allocator.AllocateImage(RTDescs::VolLightHistory(half, 1)));
	SetRenderTarget(RD::Renderer_RenderTarget::FlareBright,                 allocator.AllocateImage(RTDescs::FlareBright(quarter)));
	SetRenderTarget(RD::Renderer_RenderTarget::LensFlareColor,              allocator.AllocateImage(RTDescs::LensFlareColor(quarter)));
	SetRenderTarget(RD::Renderer_RenderTarget::HiZ,                         allocator.AllocateImage(RTDescs::HiZ(drawExtent, ClampMipCount(drawExtent, RD::HI_Z_MIP_COUNT))));
	SetRenderTarget(RD::Renderer_RenderTarget::LinearizedHiZ,               allocator.AllocateImage(RTDescs::LinearizedHiZ(drawExtent, ClampMipCount(drawExtent, RD::HI_Z_MIP_COUNT))));
	SetRenderTarget(RD::Renderer_RenderTarget::GBufferAlbedoRough,          allocator.AllocateImage(RTDescs::GBufferAlbedoRough(drawExtent)));
	SetRenderTarget(RD::Renderer_RenderTarget::GBufferNormalMaterial,       allocator.AllocateImage(RTDescs::GBufferNormalMaterial(drawExtent)));
	SetRenderTarget(RD::Renderer_RenderTarget::GBufferEmissive,             allocator.AllocateImage(RTDescs::GBufferEmissive(drawExtent)));
	SetRenderTarget(RD::Renderer_RenderTarget::DiffuseRadianceA,            allocator.AllocateImage(RTDescs::DiffuseRadiance(half, 0, ClampMipCount(half, RD::RADIANCE_MIP_COUNT))));
	SetRenderTarget(RD::Renderer_RenderTarget::DiffuseRadianceB,            allocator.AllocateImage(RTDescs::DiffuseRadiance(half, 1, ClampMipCount(half, RD::RADIANCE_MIP_COUNT))));
	SetRenderTarget(RD::Renderer_RenderTarget::GIHistoryA,                  allocator.AllocateImage(RTDescs::GIHistory(half, 0)));
	SetRenderTarget(RD::Renderer_RenderTarget::GIHistoryB,                  allocator.AllocateImage(RTDescs::GIHistory(half, 1)));
	SetRenderTarget(RD::Renderer_RenderTarget::IndirectSSGI,                allocator.AllocateImage(RTDescs::IndirectSSGI(drawExtent)));
	SetRenderTarget(RD::Renderer_RenderTarget::GIDenoisePing,               allocator.AllocateImage(RTDescs::GIDenoisePing(half)));
	SetRenderTarget(RD::Renderer_RenderTarget::ReflectRadiance,             allocator.AllocateImage(RTDescs::ReflectRadiance(half)));
	SetRenderTarget(RD::Renderer_RenderTarget::ReflectRoughness,            allocator.AllocateImage(RTDescs::ReflectRoughness(half)));
	SetRenderTarget(RD::Renderer_RenderTarget::NRDMotion,                   allocator.AllocateImage(RTDescs::NRDMotion(half)));
	SetRenderTarget(RD::Renderer_RenderTarget::NRDNormalRoughness,          allocator.AllocateImage(RTDescs::NRDNormalRoughness(half)));
	SetRenderTarget(RD::Renderer_RenderTarget::NRDViewZ,                    allocator.AllocateImage(RTDescs::NRDViewZ(half)));
	SetRenderTarget(RD::Renderer_RenderTarget::RTReflectDenoised,           allocator.AllocateImage(RTDescs::RTReflectDenoised(half)));
	SetRenderTarget(RD::Renderer_RenderTarget::NRDShadowNormalRoughness,    allocator.AllocateImage(RTDescs::NRDShadowNormalRoughness(drawExtent)));
	SetRenderTarget(RD::Renderer_RenderTarget::NRDShadowViewZ,              allocator.AllocateImage(RTDescs::NRDShadowViewZ(drawExtent)));
	SetRenderTarget(RD::Renderer_RenderTarget::RTShadowDenoised,            allocator.AllocateImage(RTDescs::RTShadowDenoised(drawExtent)));
	SetRenderTarget(RD::Renderer_RenderTarget::RTShadowPenumbra,            allocator.AllocateImage(RTDescs::RTShadowPenumbra(drawExtent)));
}

// First time setup only
void BindlessImageTable::CreateShadowMaps(RD::ShadowQuality quality, Allocator& allocator)
{
	if (!m_bAreShadowsCreated)
	{
		const uint32_t res = RD::EvaluateShadowQuality(quality);
		const Extents3D extent = { res, res, 1 };
		SetRenderTarget(RD::Renderer_RenderTarget::DirectionalCSMAtlas, allocator.AllocateImage(RTDescs::DirectionalCSMAtlas(extent)));
		SetRenderTarget(RD::Renderer_RenderTarget::FlashlightShadowMap, allocator.AllocateImage(RTDescs::FlashlightShadowMap()));
		SetRenderTarget(RD::Renderer_RenderTarget::VolumetricShadowMap, allocator.AllocateImage(RTDescs::VolumetricShadowMap()));
		m_bAreShadowsCreated = true;
	}
}

void BindlessImageTable::FreeCSMAtlas(Allocator& allocator)
{
	ASSERT(m_bAreShadowsCreated);

	if (m_cachedCsmAtlasInfo.isActive) return;

	AllocatedImage& atlas = m_renderTargets[Index(RD::Renderer_RenderTarget::DirectionalCSMAtlas)];

	ASSERT(atlas.IsValid());
	ASSERT(atlas.m_bindlessID != UINT32_MAX);
	ASSERT(atlas.m_bindlessID < static_cast<uint32_t>(m_combinedViews.size()));

	// Cache everything required to recreate the real atlas.
	m_cachedCsmAtlasInfo.width = atlas.Width();
	m_cachedCsmAtlasInfo.csmAtlasBindlessID = atlas.m_bindlessID;

	const uint32_t bindlessID = m_cachedCsmAtlasInfo.csmAtlasBindlessID;

	// Keep the render-target slot and bindless descriptor alive with a
	// negligible 1x1 image while RT shadows own directional shadowing.
	const Extents3D placeholderExtent = { 1u, 1u, 1u };

	AllocatedImage placeholder =
		allocator.AllocateImage(RTDescs::DirectionalCSMAtlas(placeholderExtent));

	ASSERT(placeholder.IsValid());

	placeholder.m_bindlessID = bindlessID;

	// Replace the CPU-side bindless entry BEFORE destroying the old view.
	// UpdateCombinedLocked also removes the old image-view hash entry.
	{
		std::scoped_lock lock(m_combinedMutex);

		UpdateCombinedLocked(
			bindlessID,
			placeholder.m_imageView,
			GetSampler(RD::Renderer_Sampler::ShadowMap));
	}

	// The large allocation can now disappear.
	allocator.FreeImage(atlas);

	atlas = std::move(placeholder);

	m_cachedCsmAtlasInfo.isActive = true;

	MarkDirty();
}

void BindlessImageTable::RecreateCSMAtlas(Allocator& allocator)
{
	ASSERT(m_bAreShadowsCreated);

	if (!m_cachedCsmAtlasInfo.isActive) return;

	AllocatedImage& atlas = m_renderTargets[Index(RD::Renderer_RenderTarget::DirectionalCSMAtlas)];

	ASSERT(atlas.IsValid());

	const uint32_t bindlessID = m_cachedCsmAtlasInfo.csmAtlasBindlessID;

	ASSERT(bindlessID != UINT32_MAX);
	ASSERT(bindlessID < static_cast<uint32_t>(m_combinedViews.size()));

	const uint32_t res = m_cachedCsmAtlasInfo.width;

	const Extents3D extent = { res, res, 1u };

	AllocatedImage restoredAtlas = allocator.AllocateImage(RTDescs::DirectionalCSMAtlas(extent));

	ASSERT(restoredAtlas.IsValid());

	restoredAtlas.m_bindlessID = bindlessID;

	// Switch the descriptor back to the real atlas.
	{
		std::scoped_lock lock(m_combinedMutex);

		UpdateCombinedLocked(
			bindlessID,
			restoredAtlas.m_imageView,
			GetSampler(RD::Renderer_Sampler::ShadowMap));
	}

	// atlas currently contains the 1x1 placeholder.
	allocator.FreeImage(atlas);

	atlas = std::move(restoredAtlas);

	m_cachedCsmAtlasInfo.isActive = false;

	MarkDirty();
}

void BindlessImageTable::UpdateCSMAtlasExtent(RD::ShadowQuality quality, Allocator& allocator)
{
	ASSERT(m_bAreShadowsCreated && "UpdateCSMAtlasExtent: shadow maps not created");

	const uint32_t res = RD::EvaluateShadowQuality(quality);
	if (m_cachedCsmAtlasInfo.isActive)
	{
		m_cachedCsmAtlasInfo.width = res;
		return;
	}

	AllocatedImage& atlas = m_renderTargets[Index(RD::Renderer_RenderTarget::DirectionalCSMAtlas)];
	ASSERT(atlas.IsValid() && "UpdateCSMAtlasExtent: CSM atlas image is invalid");

	// Preserve the bindless slot — the descriptor index must not move
	const uint32_t bindlessID = atlas.m_bindlessID;

	// Destroy old (deferred free) and rebuild at the new resolution
	allocator.FreeImage(atlas);
	atlas.Reset();

	const Extents3D extent = { res, res, 1 };
	atlas = allocator.AllocateImage(RTDescs::DirectionalCSMAtlas(extent));
	atlas.m_bindlessID = bindlessID;

	// Re-point the existing combined-sampler slot at the new view (same index)
	{
		std::scoped_lock lock(m_combinedMutex);
		UpdateCombinedLocked(bindlessID, atlas.m_imageView, GetSampler(RD::Renderer_Sampler::ShadowMap));
	}

	MarkDirty();
}

void BindlessImageTable::CreateFroxelFogTargets(Allocator& allocator)
{
	if (!m_bAreFroxelFogCreated)
	{
		SetRenderTarget(RD::Renderer_RenderTarget::FroxelScatterExtA, allocator.AllocateImage(RTDescs::FroxelScatterExt(0)));
		SetRenderTarget(RD::Renderer_RenderTarget::FroxelScatterExtB, allocator.AllocateImage(RTDescs::FroxelScatterExt(1)));
		SetRenderTarget(RD::Renderer_RenderTarget::FroxelIntegrated, allocator.AllocateImage(RTDescs::FroxelIntegrated()));

		m_bAreFroxelFogCreated = true;
		MarkDirty();
	}
}

void BindlessImageTable::FreeRenderTargets(Allocator& allocator)
{
	for (size_t i = 0; i < RD::RENDER_TARGET_COUNT; ++i)
	{
		auto slot = static_cast<RD::Renderer_RenderTarget>(i);
		if (slot == RD::Renderer_RenderTarget::DirectionalCSMAtlas ||
			slot == RD::Renderer_RenderTarget::FlashlightShadowMap ||
			slot == RD::Renderer_RenderTarget::VolumetricShadowMap ||
			slot == RD::Renderer_RenderTarget::FroxelScatterExtA   ||
			slot == RD::Renderer_RenderTarget::FroxelScatterExtB   ||
			slot == RD::Renderer_RenderTarget::FroxelIntegrated)
			continue;

		AllocatedImage& img = m_renderTargets[Index(slot)];
		if (img.IsValid())
		{
			allocator.FreeImage(img);
			img.Reset();
		}
	}
}

void BindlessImageTable::UpdateRenderTargets(Extents3D drawExtent, Allocator& allocator)
{
	FreeRenderTargets(allocator);
	CreateRenderTargets(drawExtent, allocator);
}

void BindlessImageTable::FreeFroxelFogTargets(Allocator& allocator)
{
	if (m_bAreFroxelFogCreated)
	{
		allocator.FreeImage(m_renderTargets[Index(RD::Renderer_RenderTarget::FroxelScatterExtA)]);
		allocator.FreeImage(m_renderTargets[Index(RD::Renderer_RenderTarget::FroxelScatterExtB)]);
		allocator.FreeImage(m_renderTargets[Index(RD::Renderer_RenderTarget::FroxelIntegrated)]);
		m_bAreFroxelFogCreated = false;
		MarkDirty();
	}
}

void BindlessImageTable::FreeShadowMaps(Allocator& allocator)
{
	auto freeShadowMap = [&](RD::Renderer_RenderTarget slot)
		{
			AllocatedImage& img = m_renderTargets[Index(slot)];

			if (!img.IsValid())
				return;

			allocator.FreeImage(img);
			img.Reset();
		};

	freeShadowMap(RD::Renderer_RenderTarget::DirectionalCSMAtlas);
	freeShadowMap(RD::Renderer_RenderTarget::FlashlightShadowMap);
	freeShadowMap(RD::Renderer_RenderTarget::VolumetricShadowMap);

	m_cachedCsmAtlasInfo = {};

	m_bAreShadowsCreated = false;

	MarkDirty();
}

void BindlessImageTable::SetRenderTarget(RD::Renderer_RenderTarget slot, AllocatedImage image)
{
	m_renderTargets[Index(slot)] = std::move(image);
	MarkDirty();
}

const AllocatedImage& BindlessImageTable::GetRenderTarget(RD::Renderer_RenderTarget slot) const
{
	return m_renderTargets[Index(slot)];
}

void BindlessImageTable::TransitionRenderTargetsFromUndefined(VkCommandBuffer cmd)
{
	std::vector<VkImageMemoryBarrier2> barriers;
	barriers.reserve(RD::RENDER_TARGET_COUNT);

	for (size_t i = 0; i < RD::RENDER_TARGET_COUNT; ++i)
	{
		// Transitioned in render graph
		if (i == static_cast<uint32_t>(RD::Renderer_RenderTarget::FlashlightShadowMap) ||
			i == static_cast<uint32_t>(RD::Renderer_RenderTarget::DirectionalCSMAtlas) ||
			i == static_cast<uint32_t>(RD::Renderer_RenderTarget::VolumetricShadowMap) ||
			i == static_cast<uint32_t>(RD::Renderer_RenderTarget::FroxelIntegrated) ||
			i == static_cast<uint32_t>(RD::Renderer_RenderTarget::FroxelScatterExtA) ||
			i == static_cast<uint32_t>(RD::Renderer_RenderTarget::FroxelScatterExtB)) continue;

		const AllocatedImage& img = m_renderTargets[i];
		if (!img.IsValid()) continue;

		const bool isDepth = (img.m_aspect == ImageAspect::Depth);
		VkImageAspectFlags aspectMask = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

		barriers.emplace_back(VkImageMemoryBarrier2{
			.sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
			.srcStageMask     = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
			.srcAccessMask    = VK_ACCESS_2_NONE,
			.dstStageMask     = isDepth
								? VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT
								: VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
			.dstAccessMask    = isDepth
								? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT
								: VK_ACCESS_2_SHADER_READ_BIT,
			.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout        = isDepth
								? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
								: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.image            = img.m_image,
			.subresourceRange = {
				aspectMask,
				0, VK_REMAINING_MIP_LEVELS,
				0, VK_REMAINING_ARRAY_LAYERS
			}
		});
	}

	VkDependencyInfo dep{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	dep.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
	dep.pImageMemoryBarriers    = barriers.data();
	vkCmdPipelineBarrier2(cmd, &dep);
}

// =========
// SAMPLERS
// =========

void BindlessImageTable::CreateSamplers(VkDevice device)
{
	SetSampler(RD::Renderer_Sampler::NearestClamp,
		ImageUtils::CreateSampler(device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			0.0f, 1.0f, VK_SAMPLER_MIPMAP_MODE_NEAREST));

	SetSampler(RD::Renderer_Sampler::LinearClamp,
		ImageUtils::CreateSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			RD::MAX_MIP_LEVELS, 1.0f));

	SetSampler(RD::Renderer_Sampler::HiZ,
		ImageUtils::CreateSampler(device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			static_cast<float>(RD::HI_Z_MIP_COUNT - 1), 1.0f, VK_SAMPLER_MIPMAP_MODE_NEAREST));

	SetSampler(RD::Renderer_Sampler::LinearLodClamp,
		ImageUtils::CreateSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			RD::MAX_MIP_LEVELS, 1.0f, VK_SAMPLER_MIPMAP_MODE_LINEAR));

	SetSampler(RD::Renderer_Sampler::PointBorder,
		ImageUtils::CreateSampler(device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
			0.0f, 1.0f, VK_SAMPLER_MIPMAP_MODE_NEAREST));

	SetSampler(RD::Renderer_Sampler::TaaHistory,
		ImageUtils::CreateSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			0.0f, 1.0f, VK_SAMPLER_MIPMAP_MODE_NEAREST));

	SetSampler(RD::Renderer_Sampler::Noise,
		ImageUtils::CreateSampler(device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT,
			0.0f, 1.0f, VK_SAMPLER_MIPMAP_MODE_NEAREST));

	SetSampler(RD::Renderer_Sampler::ShadowMap,
		ImageUtils::CreateSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			0.0f, 1.0f, VK_SAMPLER_MIPMAP_MODE_NEAREST));

	SetSampler(RD::Renderer_Sampler::Linear,
		ImageUtils::CreateSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT,
			RD::MAX_MIP_LEVELS, RD::ANISOTROPY_LEVEL_16));

	SetSampler(RD::Renderer_Sampler::Nearest,
		ImageUtils::CreateSampler(device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_REPEAT,
			0.0f, 1.0f, VK_SAMPLER_MIPMAP_MODE_NEAREST));

	SetSampler(RD::Renderer_Sampler::Brdf,
		ImageUtils::CreateSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			0.0f, 1.0f));

	SetSampler(RD::Renderer_Sampler::Equirect,
		ImageUtils::CreateSamplerAddr(device, VK_FILTER_LINEAR,
			VK_SAMPLER_ADDRESS_MODE_REPEAT,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			VK_LOD_CLAMP_NONE, 1.0f));

	SetSampler(RD::Renderer_Sampler::Specular,
		ImageUtils::CreateSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			static_cast<float>(RD::SPECULAR_PREFILTERED_MIP_LEVELS - 1), 0.0f));

	SetSampler(RD::Renderer_Sampler::Skybox,
		ImageUtils::CreateSampler(device, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			VK_LOD_CLAMP_NONE, 0.0f));
}

void BindlessImageTable::FreeSamplers(VkDevice device)
{
	for (auto& smp : m_samplers)
	{
		if (smp != VK_NULL_HANDLE)
			vkDestroySampler(device, smp, nullptr);
	}
	m_samplers.fill(VK_NULL_HANDLE);

	// TODO: Move asset sampler cleanup somewhere else.
	// Asset samplers are dynamic — destroy all and clear
	for (auto& smp : m_assetSamplers)
		if (smp != VK_NULL_HANDLE)
			vkDestroySampler(device, smp, nullptr);

	m_assetSamplers.clear();
	m_assetSamplerDescs.clear();
}

void BindlessImageTable::SetSampler(RD::Renderer_Sampler slot, VkSampler sampler)
{
	m_samplers[Index(slot)] = sampler;
}

VkSampler BindlessImageTable::GetSampler(RD::Renderer_Sampler slot) const
{
	return m_samplers[Index(slot)];
}

// ================
// STATIC TEXTURES
// ================

void BindlessImageTable::CreateStaticTextures(Allocator& allocator)
{
	SetStaticTexture(RD::Renderer_Texture::White,           allocator.AllocateImage(STDescs::White()));
	SetStaticTexture(RD::Renderer_Texture::Normal,          allocator.AllocateImage(STDescs::FlatNormal()));
	SetStaticTexture(RD::Renderer_Texture::MetalRough,      allocator.AllocateImage(STDescs::DefaultMetalRough()));
	SetStaticTexture(RD::Renderer_Texture::Dummy,           allocator.AllocateImage(STDescs::Dummy()));
	SetStaticTexture(RD::Renderer_Texture::DummyU8,         allocator.AllocateImage(STDescs::DummyUint8()));
	SetStaticTexture(RD::Renderer_Texture::Checkerboard,    allocator.AllocateImage(STDescs::ErrorCheckerboard()));
	SetStaticTexture(RD::Renderer_Texture::RainbowLut,      allocator.AllocateImage(STDescs::RainbowLUT()));
	SetStaticTexture(RD::Renderer_Texture::HilbertCurveLut, allocator.AllocateImage(STDescs::HilbertCurveLUT()));
	SetStaticTexture(RD::Renderer_Texture::CookieGobo,      allocator.AllocateImage(STDescs::CookieGobo()));
	SetStaticTexture(RD::Renderer_Texture::Brdf,            allocator.AllocateImage(EnvDescs::BRDFLut()));
	//SetStaticTexture(RD::Renderer_Texture::SMAAArea,        allocator.AllocateImage(STDescs::SMAAArea()));
	//SetStaticTexture(RD::Renderer_Texture::SMAASearch,      allocator.AllocateImage(STDescs::SMAASearch()));
}

size_t BindlessImageTable::CalcStaticTexturesStagingSize() const
{
	size_t total = 0;
	for (const auto& tex : m_staticTextures)
	{
		if (!tex.IsValid()) continue;
		const size_t w      = tex.Width();
		const size_t h      = tex.Height();
		const size_t d      = std::max(tex.Depth(), 1u);
		const size_t pixels = w * h * d * tex.m_pixelBytes;
		total += AllocatedBuffer::AlignUp(pixels, static_cast<size_t>(16));
	}
	return total;
}

void BindlessImageTable::UploadStaticTextures(StagingBuffer& staging, VkCommandBuffer cmd)
{
	auto& st = m_staticTextures;

	// --- 1x1 RGBA8 trivial textures ---
	const uint32_t white       = glm::packUnorm4x8(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
	const uint32_t flatNormal  = glm::packUnorm4x8(glm::vec4(0.5f, 0.5f, 1.0f, 1.0f));
	const uint32_t black       = glm::packUnorm4x8(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
	const uint8_t  metalRough[4] = { 0, static_cast<uint8_t>(0.5f * 255), 0, 255 };
	const uint32_t dummyBlack  = 0u;
	const uint8_t  dummyU8     = 0u;

	// --- Checkerboard 16x16 RGBA8 ---
	const uint32_t magenta = glm::packUnorm4x8(glm::vec4(1, 0, 1, 1));
	std::array<uint32_t, 16 * 16> checkerboard{};
	for (int x = 0; x < 16; ++x)
		for (int y = 0; y < 16; ++y)
			checkerboard[static_cast<size_t>(y * 16 + x)] = ((x % 2) ^ (y % 2)) ? magenta : black;

	// --- Rainbow LUT 256x1 RGBA8 ---
	std::vector<uint32_t> rainbowLut(256);
	for (uint32_t x = 0; x < 256; ++x)
	{
		float t   = static_cast<float>(x) / 255.0f;
		float hue = 0.02f + 0.90f * t - 0.06f;
		hue -= std::floor(hue);
		glm::vec3 rgb = HsvToRgb(hue, 0.95f, 1.0f);
		rgb.g *= 0.9f;
		rainbowLut[x] = glm::packUnorm4x8(glm::vec4(rgb, 1.0f));
	}

	// --- Hilbert curve LUT 64x64 R16U ---
	std::vector<uint16_t> hilbertLut(64 * 64);
	for (int x = 0; x < 64; ++x)
		for (int y = 0; y < 64; ++y)
			hilbertLut[static_cast<size_t>(x + 64 * y)] = static_cast<uint16_t>(HilbertIndex(x, y));

	// --- Cookie gobo R8 from disk ---
	int cookieW, cookieH, cookieCh;
	stbi_uc* cookieData = stbi_load("res/assets/light_cookie.png", &cookieW, &cookieH, &cookieCh, 1);
	ASSERT(cookieData && "Failed to load light_cookie.png");

	TextureUploadDesc uploads[] =
	{
		{.image = &st[Index(RD::Renderer_Texture::White)],           .pixelData = &white,
		  .pixelBytes = st[Index(RD::Renderer_Texture::White)].m_pixelBytes,           .strategy = MipStrategy::GenerateOnGPU },
		{.image = &st[Index(RD::Renderer_Texture::Normal)],          .pixelData = &flatNormal,
		  .pixelBytes = st[Index(RD::Renderer_Texture::Normal)].m_pixelBytes,          .strategy = MipStrategy::GenerateOnGPU },
		{.image = &st[Index(RD::Renderer_Texture::MetalRough)],      .pixelData = metalRough,
		  .pixelBytes = st[Index(RD::Renderer_Texture::MetalRough)].m_pixelBytes,      .strategy = MipStrategy::GenerateOnGPU },
		{.image = &st[Index(RD::Renderer_Texture::Dummy)],           .pixelData = &dummyBlack,
		  .pixelBytes = st[Index(RD::Renderer_Texture::Dummy)].m_pixelBytes,           .strategy = MipStrategy::SingleLevel },
		{.image = &st[Index(RD::Renderer_Texture::DummyU8)],         .pixelData = &dummyU8,
		  .pixelBytes = st[Index(RD::Renderer_Texture::DummyU8)].m_pixelBytes,         .strategy = MipStrategy::SingleLevel },
		{.image = &st[Index(RD::Renderer_Texture::Checkerboard)],    .pixelData = checkerboard.data(),
		  .pixelBytes = st[Index(RD::Renderer_Texture::Checkerboard)].m_pixelBytes,    .strategy = MipStrategy::GenerateOnGPU },
		{.image = &st[Index(RD::Renderer_Texture::RainbowLut)],      .pixelData = rainbowLut.data(),
		  .pixelBytes = st[Index(RD::Renderer_Texture::RainbowLut)].m_pixelBytes,      .strategy = MipStrategy::SingleLevel },
		{.image = &st[Index(RD::Renderer_Texture::HilbertCurveLut)], .pixelData = hilbertLut.data(),
		  .pixelBytes = st[Index(RD::Renderer_Texture::HilbertCurveLut)].m_pixelBytes, .strategy = MipStrategy::SingleLevel },
		{.image = &st[Index(RD::Renderer_Texture::CookieGobo)],      .pixelData = cookieData,
		  .pixelBytes = st[Index(RD::Renderer_Texture::CookieGobo)].m_pixelBytes,      .strategy = MipStrategy::SingleLevel },
	};

	staging.ExecuteTextureBatch(cmd, uploads);

	stbi_image_free(cookieData);
}

void BindlessImageTable::FreeStaticTextures(Allocator& allocator)
{
	for (auto& tex : m_staticTextures)
	{
		if (tex.IsValid())
		{
			allocator.FreeImage(tex);
			tex.Reset();
		}
	}
}

void BindlessImageTable::SetStaticTexture(RD::Renderer_Texture slot, AllocatedImage image)
{
	m_staticTextures[Index(slot)] = std::move(image);
	MarkDirty();
}

const AllocatedImage& BindlessImageTable::GetStaticTexture(RD::Renderer_Texture slot) const
{
	return m_staticTextures[Index(slot)];
}

// =================
// ENVIRONMENT SETS
// =================

void BindlessImageTable::CreateEnvironmentSets(uint32_t setCount, Allocator& allocator)
{
	for (uint32_t i = 0; i < setCount; ++i)
	{
		EnvironmentSet envSet;
		envSet.setIndex   = i;
		envSet.skybox     = allocator.AllocateImage(EnvDescs::Skybox());
		envSet.specular   = allocator.AllocateImage(EnvDescs::Specular(RD::SPECULAR_PREFILTERED_MIP_LEVELS));

		const uint32_t specMips = envSet.specular.m_mipLevels;
		envSet.specularPCs.resize(specMips);
		for (uint32_t mip = 0; mip < specMips; ++mip)
		{
			envSet.specularPCs[mip].roughness    = static_cast<float>(mip) / static_cast<float>(specMips - 1);
			envSet.specularPCs[mip].sampleCount  = RD::PREFILTER_SAMPLE_COUNT;
			envSet.specularPCs[mip].width        = std::max(1u, envSet.specular.Width()  >> mip);
			envSet.specularPCs[mip].height       = std::max(1u, envSet.specular.Height() >> mip);
		}

		AddEnvironmentSet(std::move(envSet));
	}
}

void BindlessImageTable::PreallocateEquirects(
	std::span<const char* const> hdrPaths,
	Allocator&                   allocator)
{
	ASSERT(hdrPaths.size() <= m_environmentSets.size());

	for (uint32_t i = 0; i < static_cast<uint32_t>(hdrPaths.size()); ++i)
	{
		int w, h, ch;
		float* pixels = stbi_loadf(hdrPaths[i], &w, &h, &ch, 4);
		ASSERT(pixels && "PreallocateEquirects: failed to probe HDR dimensions");
		stbi_image_free(pixels);

		m_environmentSets[i].equirect = allocator.AllocateImage(ImageDesc{
			.format = Vulkan_Format::RGBA32F,
			.extent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 },
			.usage  = Vulkan_ImageUsage::ComputeRWTransfer
		});

		ASSERT(m_environmentSets[i].equirect.IsValid() && 
			"PreallocateEquirects: image allocation failed");
	}
}

void BindlessImageTable::UploadEquirects(
	std::span<const char* const> hdrPaths,
	Allocator&                   allocator,
	VkCommandBuffer              cmd)
{
	ASSERT(hdrPaths.size() <= m_environmentSets.size());

	struct LoadedHDR
	{
		float*   pixels      = nullptr;
		uint32_t envSetIndex = 0;
	};

	std::vector<LoadedHDR>         loaded;
	std::vector<TextureUploadDesc> uploads;
	loaded.reserve(hdrPaths.size());
	uploads.reserve(hdrPaths.size());

	for (uint32_t i = 0; i < static_cast<uint32_t>(hdrPaths.size()); ++i)
	{
		ASSERT(m_environmentSets[i].equirect.IsValid() &&
			"UploadEquirects: equirect not preallocated — call PreallocateEquirects first");

		int w, h, ch;
		float* pixels = stbi_loadf(hdrPaths[i], &w, &h, &ch, 4);
		ASSERT(pixels && "UploadEquirects: failed to load HDR pixels");

		loaded.push_back({ pixels, i });
		uploads.emplace_back(TextureUploadDesc{
			.image      = &m_environmentSets[i].equirect,
			.pixelData  = pixels,
			.pixelBytes = m_environmentSets[i].equirect.m_pixelBytes,
			.strategy   = MipStrategy::SingleLevel
		});
	}

	allocator.GlobalStaging.ExecuteTextureBatch(cmd, uploads);

	for (auto& l : loaded)
		stbi_image_free(l.pixels);
}

void BindlessImageTable::FreeEquirects(Allocator& allocator)
{
	for (auto& env : m_environmentSets)
	{
		if (env.equirect.IsValid())
		{
			allocator.FreeImage(env.equirect);
			env.equirect.Reset();
		}
	}
}

void BindlessImageTable::FreeEnvironmentSets(Allocator& allocator)
{
	for (auto& env : m_environmentSets)
	{
		if (!env.IsValid()) continue;
		ASSERT(!env.equirect.IsValid() && "FreeEquirect must be called before shutdown");
		if (env.skybox.IsValid())     allocator.FreeImage(env.skybox);
		if (env.specular.IsValid())   allocator.FreeImage(env.specular);
		env.Reset();
	}
}

void BindlessImageTable::AddEnvironmentSet(EnvironmentSet envSet)
{
	for (uint32_t i = 0; i < static_cast<uint32_t>(m_environmentSets.size()); ++i)
	{
		if (!m_environmentSets[i].IsValid())
		{
			envSet.setIndex      = i;
			m_environmentSets[i] = std::move(envSet);
			MarkDirty();
			return;
		}
	}
	ASSERT(false && "No free EnvironmentSet slots");
}

const EnvironmentSet& BindlessImageTable::GetEnvironmentSet(uint32_t index) const
{
	ASSERT(index < static_cast<uint32_t>(m_environmentSets.size()));
	return m_environmentSets[index];
}

EnvironmentSet& BindlessImageTable::GetEnvironmentSetMutable(uint32_t index)
{
	ASSERT(index < static_cast<uint32_t>(m_environmentSets.size()));
	return m_environmentSets[index];
}

uint32_t BindlessImageTable::EnvironmentSetCount() const noexcept
{
	return static_cast<uint32_t>(
		std::ranges::count_if(m_environmentSets, [](const EnvironmentSet& e) { return e.IsValid(); }));
}

// ===============
// ASSET TEXTURES
// ===============

uint32_t BindlessImageTable::PushAssetTexture(AllocatedImage image)
{
	ASSERT(image.IsValid());
	for (uint32_t i = 0; i < static_cast<uint32_t>(m_assetTextures.size()); ++i)
	{
		if (!m_assetTextures[i].IsValid())
		{
			m_assetTextures[i] = std::move(image);
			MarkDirty();
			return i;
		}
	}
	const uint32_t idx = static_cast<uint32_t>(m_assetTextures.size());
	m_assetTextures.push_back(std::move(image));
	MarkDirty();
	return idx;
}

const AllocatedImage& BindlessImageTable::GetAssetTexture(uint32_t index) const
{
	ASSERT(index < static_cast<uint32_t>(m_assetTextures.size()) && m_assetTextures[index].IsValid());
	return m_assetTextures[index];
}

void BindlessImageTable::FreeAssetTexture(uint32_t index)
{
	ASSERT(index < static_cast<uint32_t>(m_assetTextures.size()) && m_assetTextures[index].IsValid());
	m_assetTextures[index].Reset();
	MarkDirty();
}

AllocatedImage& BindlessImageTable::GetAssetTextureMutable(uint32_t index)
{
	ASSERT(index < static_cast<uint32_t>(m_assetTextures.size()));
	return m_assetTextures[index];
}

bool BindlessImageTable::IsAssetTextureValid(uint32_t index) const noexcept
{
	return index < static_cast<uint32_t>(m_assetTextures.size())
		&& m_assetTextures[index].IsValid();
}

uint32_t BindlessImageTable::ResolveAssetSampler(const SamplerDesc& desc, VkDevice device)
{
	for (uint32_t i = 0; i < static_cast<uint32_t>(m_assetSamplers.size()); ++i)
	{
		if (m_assetSamplerDescs[i].isLinear    == desc.isLinear   &&
			m_assetSamplerDescs[i].isMipMapped == desc.isMipMapped &&
			m_assetSamplerDescs[i].anisotropy  == desc.anisotropy)
			return i;
	}

	VkFilter filter    = desc.isLinear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
	VkSamplerMipmapMode mipMode  = desc.isMipMapped
		? VK_SAMPLER_MIPMAP_MODE_LINEAR
		: VK_SAMPLER_MIPMAP_MODE_NEAREST;
	float maxLod = desc.isMipMapped ? VK_LOD_CLAMP_NONE : 0.0f;

	VkSampler sampler = ImageUtils::CreateSampler(
		device,
		filter,
		VK_SAMPLER_ADDRESS_MODE_REPEAT,
		maxLod,
		desc.anisotropy,
		mipMode);

	const uint32_t slot = static_cast<uint32_t>(m_assetSamplers.size());
	m_assetSamplers.push_back(sampler);
	m_assetSamplerDescs.push_back(desc);
	return slot;
}

VkSampler BindlessImageTable::ResolveDefaultAssetSampler(const AllocatedImage& img) const noexcept
{
	if (img.m_mipLevels <= 1)
		return GetSampler(RD::Renderer_Sampler::LinearClamp);
	return GetSampler(RD::Renderer_Sampler::Linear); // trilinear repeat + aniso
}

std::vector<uint32_t> BindlessImageTable::UploadAssetTextures(
	SceneUploadBatch& batch,
	VkDevice          device,
	Allocator&        allocator,
	StagingBuffer&    staging,
	VkCommandBuffer   cmd)
{
	std::vector<uint32_t> ownedSlots;
	if (batch.textures.empty()) return ownedSlots;

	struct TexSlot { uint32_t assetIndex; uint32_t tableIndex; };
	std::vector<TexSlot> validSlots;
	validSlots.reserve(batch.textures.size());

	for (uint32_t i = 0; i < static_cast<uint32_t>(batch.textures.size()); ++i)
	{
		TextureDesc& desc = batch.textures[i];
		if (!desc.IsValid())
		{
			desc.bindlessID = GetStaticTexture(RD::Renderer_Texture::Checkerboard).m_bindlessID;
			continue;
		}

		ImageDesc imgDesc{};
		imgDesc.format = ResolveAssetFormat(desc);
		imgDesc.extent = { desc.width, desc.height, 1u };
		imgDesc.usage = Vulkan_ImageUsage::TextureSampled;
		imgDesc.mipLevels = static_cast<uint32_t>(desc.mips.size());
		imgDesc.debugName = desc.debugName.c_str();

		AllocatedImage img = allocator.AllocateImage(imgDesc);

		VkSampler sampler = VK_NULL_HANDLE;
		if (i < static_cast<uint32_t>(batch.samplers.size())
			&& batch.samplers[i].rendererSlot == UINT32_MAX)
		{
			uint32_t slot = ResolveAssetSampler(batch.samplers[i], device);
			sampler = m_assetSamplers[slot];
			batch.samplers[i].rendererSlot = slot;
		}
		if (sampler == VK_NULL_HANDLE)
			sampler = ResolveDefaultAssetSampler(img);

		img.m_bindlessID = PushCombined(img.m_imageView, sampler);
		desc.bindlessID = img.m_bindlessID;

		const uint32_t tableIdx = PushAssetTexture(std::move(img));
		ownedSlots.push_back(tableIdx);
		validSlots.push_back({ i, tableIdx });
	}

	if (!validSlots.empty())
	{
		std::vector<TextureUploadDesc> uploads;
		uploads.reserve(validSlots.size());

		for (const auto& slot : validSlots)
		{
			TextureDesc& desc = batch.textures[slot.assetIndex];
			AllocatedImage& img = m_assetTextures[slot.tableIndex];

			uploads.emplace_back(TextureUploadDesc{
				.image = &img,
				.pixelData = desc.pixelData.data(),
				.byteSize = desc.pixelData.size(),
				.mips = desc.mips,
				.strategy = MipStrategy::Precomputed
				});
		}

		staging.ExecuteTextureBatch(cmd, uploads);
	}

	for (auto& desc : batch.textures)
	{
		desc.pixelData.clear();
		desc.pixelData.shrink_to_fit();
	}

	MarkDirty();
	return ownedSlots;
}

// =====================
// DESCRIPTOR ARRAYS
// =====================

uint32_t BindlessImageTable::PushCombinedLocked(VkImageView view, VkSampler sampler)
{
	ASSERT(view != VK_NULL_HANDLE && sampler != VK_NULL_HANDLE);
	auto key = ImageViewSamplerKey{ view, sampler };
	if (auto it = m_combinedViewHashToID.find(key); it != m_combinedViewHashToID.end())
		return it->second;

	const uint32_t index = static_cast<uint32_t>(m_combinedViews.size());
	m_combinedViews.emplace_back(VkDescriptorImageInfo{ sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
	m_combinedViewHashToID[key] = index;
	return index;
}

uint32_t BindlessImageTable::PushCombined(VkImageView view, VkSampler sampler)
{
	std::scoped_lock lock(m_combinedMutex);
	return PushCombinedLocked(view, sampler);
}

void BindlessImageTable::PushCombinedBatch(std::span<AllocatedImage> images, VkSampler sampler)
{
	std::scoped_lock lock(m_combinedMutex);
	for (AllocatedImage& img : images)
	{
		if (!img.IsValid()) continue;
		img.m_bindlessID = PushCombinedLocked(img.m_imageView, sampler);
	}
	MarkDirty();
}

uint32_t BindlessImageTable::PushSamplerCubeLocked(VkImageView view, VkSampler sampler)
{
	ASSERT(view != VK_NULL_HANDLE && sampler != VK_NULL_HANDLE);
	auto key = ImageViewSamplerKey{ view, sampler };
	if (auto it = m_samplerCubeViewHashToID.find(key); it != m_samplerCubeViewHashToID.end())
		return it->second;

	const uint32_t index = static_cast<uint32_t>(m_samplerCubeViews.size());
	m_samplerCubeViews.emplace_back(VkDescriptorImageInfo{ sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL });
	m_samplerCubeViewHashToID[key] = index;
	return index;
}

uint32_t BindlessImageTable::PushSamplerCube(VkImageView view, VkSampler sampler)
{
	std::scoped_lock lock(m_samplerCubeMutex);
	return PushSamplerCubeLocked(view, sampler);
}

void BindlessImageTable::UpdateCombinedLocked(uint32_t index, VkImageView view, VkSampler sampler)
{
	ASSERT(index < static_cast<uint32_t>(m_combinedViews.size()));
	ASSERT(view != VK_NULL_HANDLE && sampler != VK_NULL_HANDLE);

	// Drop the stale key currently mapped to this slot
	const VkDescriptorImageInfo& prev = m_combinedViews[index];
	if (prev.imageView != VK_NULL_HANDLE)
	{
		auto prevKey = ImageViewSamplerKey{ prev.imageView, prev.sampler };
		if (auto it = m_combinedViewHashToID.find(prevKey);
			it != m_combinedViewHashToID.end() && it->second == index)
			m_combinedViewHashToID.erase(it);
	}

	m_combinedViews[index] = VkDescriptorImageInfo{ sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
	m_combinedViewHashToID[ImageViewSamplerKey{ view, sampler }] = index;
}

void BindlessImageTable::RegisterShadowMapsAsCombined(VkSampler shadowSampler)
{
	std::scoped_lock lock(m_combinedMutex);
	auto push = [&](RD::Renderer_RenderTarget slot)
	{
		AllocatedImage& img = m_renderTargets[Index(slot)];
		if (img.IsValid())
		{
			img.m_bindlessID = PushCombinedLocked(img.m_imageView, shadowSampler);
		}
	};
	push(RD::Renderer_RenderTarget::DirectionalCSMAtlas);
	push(RD::Renderer_RenderTarget::FlashlightShadowMap);
	push(RD::Renderer_RenderTarget::VolumetricShadowMap);
	m_shadowMapCombinedEnd = static_cast<uint32_t>(m_combinedViews.size());
}

void BindlessImageTable::RegisterStaticTexturesAsCombined(VkSampler genericSampler)
{
	std::scoped_lock lock(m_combinedMutex);
	for (AllocatedImage& img : m_staticTextures)
	{
		if (img.IsValid())
		{
			img.m_bindlessID = PushCombinedLocked(img.m_imageView, genericSampler);
		}
	}
	m_staticTextureCombinedEnd = static_cast<uint32_t>(m_combinedViews.size());
}

void BindlessImageTable::RegisterEnvironmentSetAsCube(
	uint32_t  envSetIndex,
	VkSampler skyboxSampler,
	VkSampler specularSampler,
	VkSampler irradianceSampler)
{
	ASSERT(envSetIndex < static_cast<uint32_t>(m_environmentSets.size()));
	EnvironmentSet& env = m_environmentSets[envSetIndex];

	std::scoped_lock lock(m_samplerCubeMutex);
	if (env.skybox.IsValid())
		env.skybox.m_bindlessID     = PushSamplerCubeLocked(env.skybox.m_imageView,     skyboxSampler);
	if (env.specular.IsValid())
		env.specular.m_bindlessID   = PushSamplerCubeLocked(env.specular.m_imageView,   specularSampler);
	MarkDirty();
}

// ============================
// Descriptor array image bake
// ============================

void BindlessImageTable::BuildInitialCombinedSamplerArray()
{
	// Shadow maps first — fixed indices 0 and 1
	RegisterShadowMapsAsCombined(GetSampler(RD::Renderer_Sampler::ShadowMap));

	// Static textures follow immediately after
	// Each texture uses the sampler most appropriate for its type
	auto pushStatic = [&](RD::Renderer_Texture slot, RD::Renderer_Sampler sampler)
	{
		AllocatedImage& img = m_staticTextures[Index(slot)];
		img.m_bindlessID = PushCombinedLocked(img.m_imageView, GetSampler(sampler));
	};

	std::scoped_lock lock(m_combinedMutex);

	pushStatic(RD::Renderer_Texture::White,           RD::Renderer_Sampler::LinearClamp);
	pushStatic(RD::Renderer_Texture::Normal,          RD::Renderer_Sampler::LinearClamp);
	pushStatic(RD::Renderer_Texture::MetalRough,      RD::Renderer_Sampler::LinearClamp);
	pushStatic(RD::Renderer_Texture::Dummy,           RD::Renderer_Sampler::NearestClamp);
	pushStatic(RD::Renderer_Texture::DummyU8,         RD::Renderer_Sampler::NearestClamp);
	pushStatic(RD::Renderer_Texture::Checkerboard,    RD::Renderer_Sampler::LinearClamp);
	pushStatic(RD::Renderer_Texture::RainbowLut,      RD::Renderer_Sampler::LinearClamp);
	pushStatic(RD::Renderer_Texture::HilbertCurveLut, RD::Renderer_Sampler::Noise);
	pushStatic(RD::Renderer_Texture::CookieGobo,      RD::Renderer_Sampler::LinearClamp);
	pushStatic(RD::Renderer_Texture::Brdf,            RD::Renderer_Sampler::Brdf);
	//pushStatic(RD::Renderer_Texture::SMAAArea,        RD::Renderer_Sampler::LinearLodClamp);
	//pushStatic(RD::Renderer_Texture::SMAASearch,      RD::Renderer_Sampler::LinearLodClamp);

	m_staticTextureCombinedEnd = static_cast<uint32_t>(m_combinedViews.size());

	MarkDirty();
}

void BindlessImageTable::BuildInitialSamplerCubeArray()
{
	std::scoped_lock lock(m_samplerCubeMutex);

	for (auto& env : m_environmentSets)
	{
		if (!env.IsValid()) continue;

		env.specular.m_bindlessID   = PushSamplerCubeLocked(env.specular.m_imageView,   GetSampler(RD::Renderer_Sampler::Specular));
		env.skybox.m_bindlessID     = PushSamplerCubeLocked(env.skybox.m_imageView,     GetSampler(RD::Renderer_Sampler::Skybox));
	}

	MarkDirty();
}
