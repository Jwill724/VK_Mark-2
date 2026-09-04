#pragma once

#include "EngineTypes.h"
#include "../VulkanTypes.h"

// TODO: Add more predefined extents in this header
#include "../../RendererDefinitions.h"
namespace RD = RendererDefinitions;

namespace RenderTargetDescs
{
	// --- Full resolution matches swapchain ---

	inline ImageDesc HDRScene(Extents3D ext)
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = ext,
				 .usage = Vulkan_ImageUsage::DrawColor,
				 .debugName = "HDRScene" };
	}

	inline ImageDesc TransparentAccumulation(Extents3D ext)
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = ext,
				 .usage  = Vulkan_ImageUsage::DrawColor,.debugName = "TransparentAccumulation" };
	}

	inline ImageDesc TransparentVelocityAccum(Extents3D ext)
	{
		return { .format = Vulkan_Format::RG16F, .extent = ext,
				 .usage = Vulkan_ImageUsage::DrawColor, .debugName = "TransparentVelocityAccum" };
	}

	inline ImageDesc TransparentRevealage(Extents3D ext)
	{
		return { .format = Vulkan_Format::R16F, .extent = ext,
				 .usage  = Vulkan_ImageUsage::DrawColor, .debugName = "TransparentRevealage" };
	}

	inline ImageDesc DepthResolved(Extents3D ext)
	{
		return { .format = Vulkan_Format::D32, .extent = ext,
				 .usage  = Vulkan_ImageUsage::DrawDepth, .debugName = "DepthResolved" };
	}

	inline ImageDesc PrevDepth(Extents3D ext)
	{
		return { .format = Vulkan_Format::D32, .extent = ext,
				 .usage  = Vulkan_ImageUsage::DrawDepth, .debugName = "PrevDepth" };
	}

	inline ImageDesc Velocity(Extents3D ext)
	{
		return { .format = Vulkan_Format::RG16F, .extent = ext,
				 .usage  = Vulkan_ImageUsage::DrawColor, .debugName = "Velocity" };
	}

	inline ImageDesc Visibility(Extents3D ext)
	{
		return { .format = Vulkan_Format::RG32U, .extent = ext,
				 .usage  = Vulkan_ImageUsage::DrawColor, .debugName = "Visibility" };
	}

	inline ImageDesc ViewNormals(Extents3D ext)
	{
		return { .format = Vulkan_Format::RG8unorm, .extent = ext,
				 .usage  = Vulkan_ImageUsage::DrawColor, .debugName = "ViewNormals" };
	}

	inline ImageDesc PrevViewNormals(Extents3D ext)
	{
		return { .format = Vulkan_Format::RG8unorm, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeRWTransfer, .debugName = "PrevViewNormals" };
	}

	inline ImageDesc ToneMap(Extents3D ext)
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeRWTransfer, .debugName = "ToneMap" };
	}

	inline ImageDesc ColorHistory(Extents3D ext, uint32_t index)
	{
		// index disambiguates the two ping-pong slots in debug tools
		return { .format = Vulkan_Format::RGBA16F, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeRWTransfer,
				 .debugName = (index == 0) ? "ColorHistoryA" : "ColorHistoryB" };
	}

	inline ImageDesc AAColor(Extents3D ext)
	{
		return { .format = Vulkan_Format::BGRpacked, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeRWTransfer, .debugName = "AAColor" };
	}

	inline ImageDesc GBufferAlbedoRough(Extents3D ext)
	{
		return { .format = Vulkan_Format::RGBA8unorm, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeOnly, .debugName = "GBufferAlbedoRough" };
	}

	inline ImageDesc GBufferNormalMaterial(Extents3D ext)
	{
		return { .format = Vulkan_Format::R32U, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeOnly, .debugName = "GBufferNormalMaterial" };
	}

	inline ImageDesc GBufferEmissive(Extents3D ext)
	{
		return { .format = Vulkan_Format::BGRpacked, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeOnly,.debugName = "GBufferEmissive" };
	}

	inline ImageDesc PostNonAAComposite(Extents3D ext)
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeRWTransfer, .debugName = "PostNonAAComposite" };
	}

	inline ImageDesc ScreenSpaceShadowMask(Extents3D ext)
	{
		return { .format = Vulkan_Format::R8unorm, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeOnly, .debugName = "ScreenSpaceShadowMask" };
	}

	inline ImageDesc SMAAEdges(Extents3D ext)
	{
		return { .format = Vulkan_Format::RG8unorm, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeOnly, .debugName = "SMAAEdges" };
	}

	inline ImageDesc SMAAWeights(Extents3D ext)
	{
		return { .format = Vulkan_Format::RGBA8unorm, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeOnly, .debugName = "SMAAWeights" };
	}

	inline ImageDesc BentNormalAO(Extents3D ext)
	{
		return { .format = Vulkan_Format::RGBA8unorm, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeOnly, .debugName = "BentNormalAO" };
	}

	inline ImageDesc IndirectSSGI(Extents3D ext)
	{
		return { .format = Vulkan_Format::BGRpacked, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeOnly, .debugName = "IndirectSSGI" };
	}

	// --- Half resolution ---

	// --- NRD I/O  ---
	inline ImageDesc NRDMotion(Extents3D halfExt)
	{
		return { .format = Vulkan_Format::RG16F, .extent = halfExt,
				 .usage = Vulkan_ImageUsage::ComputeOnly, .debugName = "NRDMotion" };
	}
	inline ImageDesc NRDNormalRoughness(Extents3D halfExt)
	{
		return { .format = Vulkan_Format::ABGRpacked, .extent = halfExt,
				 .usage = Vulkan_ImageUsage::ComputeOnly, .debugName = "NRDNormalRoughness" };
	}
	inline ImageDesc NRDViewZ(Extents3D halfExt)
	{
		return { .format = Vulkan_Format::R32F, .extent = halfExt,
				 .usage = Vulkan_ImageUsage::ComputeOnly, .debugName = "NRDViewZ" };
	}

	inline ImageDesc RTReflectDenoised(Extents3D halfExt)
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = halfExt,
				 .usage = Vulkan_ImageUsage::ComputeOnly, .debugName = "ReflectDenoised" };
	}

	inline ImageDesc ReflectRadiance(Extents3D halfExt)
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = halfExt,
				 .usage  = Vulkan_ImageUsage::ComputeOnly, .debugName = "ReflectRadiance" };
	}

	inline ImageDesc ReflectRoughness(Extents3D halfExt)
	{
		return { .format = Vulkan_Format::R16F, .extent = halfExt,
				 .usage  = Vulkan_ImageUsage::ComputeOnly, .debugName = "ReflectRoughness" };
	}

	inline ImageDesc ReflectReprojection(Extents3D halfExt)
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = halfExt,
				 .usage  = Vulkan_ImageUsage::ComputeOnly, .debugName = "ReflectReprojection" };
	}

	inline ImageDesc ReflectPrefiltered(Extents3D halfExt)
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = halfExt,
				 .usage  = Vulkan_ImageUsage::ComputeOnly, .debugName = "ReflectPrefiltered" };
	}

	inline ImageDesc ReflectVariance(Extents3D halfExt, uint32_t index)
	{
		return { .format = Vulkan_Format::RG16F, .extent = halfExt,
				 .usage  = Vulkan_ImageUsage::ComputeOnly,
				 .debugName = (index == 0) ? "ReflectVarianceA" : "ReflectVarianceB" };
	}

	inline ImageDesc ReflectRayInfo(Extents3D halfExt)
	{
		return { .format = Vulkan_Format::RGBA32F, .extent = halfExt,
				 .usage = Vulkan_ImageUsage::ComputeOnly, .debugName = "ReflectRayInfo" };
	}

	inline ImageDesc ReflectHistory(Extents3D halfExt, uint32_t index)
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = halfExt,
				 .usage  = Vulkan_ImageUsage::ComputeOnly,
				 .debugName = (index == 0) ? "ReflectHistoryA" : "ReflectHistoryB" };
	}

	inline ImageDesc AOEdgeInfo(Extents3D ext)
	{
		return { .format = Vulkan_Format::R8unorm, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeOnly,.debugName = "AOEdgeInfo" };
	}

	inline ImageDesc AORaw(Extents3D ext)
	{
		return { .format = Vulkan_Format::R8unorm, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeOnly, .debugName = "AORaw" };
	}

	inline ImageDesc AOTemp(Extents3D ext)
	{
		return { .format = Vulkan_Format::R8unorm, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeOnly, .debugName = "AOTemp" };
	}

	inline ImageDesc VolumetricLight(Extents3D halfExt)
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = halfExt,
				 .usage  = Vulkan_ImageUsage::ComputeOnly, .debugName = "VolumetricLight" };
	}

	inline ImageDesc VolumetricBlur(Extents3D halfExt)
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = halfExt,
				 .usage  = Vulkan_ImageUsage::ComputeOnly, .debugName = "VolumetricBlur" };
	}

	inline ImageDesc VolLightHistory(Extents3D halfExt, uint32_t index)
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = halfExt,
				 .usage  = Vulkan_ImageUsage::ComputeOnly,
				 .debugName = (index == 0) ? "VolLightHistoryA" : "VolLightHistoryB" };
	}

	inline ImageDesc BentNormalAOHalf(Extents3D ext)
	{
		return { .format = Vulkan_Format::RGBA8unorm, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeOnly, .debugName = "BentNormalAOHalf" };
	}

	inline ImageDesc DiffuseRadiance(Extents3D halfExt, uint32_t index, uint32_t mipCount)
	{
		return { .format = Vulkan_Format::BGRpacked, .extent = halfExt,
				 .usage  = Vulkan_ImageUsage::ComputeOnly,
				 .mipLevels = mipCount, .bPerMipStorage = true,
				 .debugName = (index == 0) ? "DiffuseRadianceA" : "DiffuseRadianceB" };
	}

	inline ImageDesc GIHistory(Extents3D halfExt, uint32_t index)
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = halfExt,
				 .usage  = Vulkan_ImageUsage::ComputeOnly,
				 .debugName = (index == 0) ? "GIHistoryA" : "GIHistoryB" };
	}

	inline ImageDesc GIDenoisePing(Extents3D halfExt)
	{
		return { .format = Vulkan_Format::BGRpacked, .extent = halfExt,
				 .usage  = Vulkan_ImageUsage::ComputeOnly,
				 .debugName = "GIDenoisePing" };
	}

	// render extent / 8
	inline ImageDesc ShadingLowHistory(Extents3D eighthExt, uint32_t index)
	{
		return { .format = Vulkan_Format::RG16F, .extent = eighthExt,
				 .usage = Vulkan_ImageUsage::ComputeOnly,
				 .debugName = (index == 0) ? "ShadingLowA" : "ShadingLowB" };
	}

	inline ImageDesc ShadingSignalReduced(Extents3D halfExt)
	{
		return { .format = Vulkan_Format::RG16F, .extent = halfExt,
				 .usage = Vulkan_ImageUsage::ComputeOnly,
				 .debugName = "ShadingSignalReduced" };
	}

	// CMAA2 uses { halfWidth, fullHeight }
	inline ImageDesc CMAA2WorkingEdges(Extents3D ext)
	{
		return { .format = Vulkan_Format::R8U, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeOnly, .debugName = "CMAA2WorkingEdges" };
	}

	inline ImageDesc BloomMipchain(Extents3D halfExt)
	{
		return { .format = Vulkan_Format::BGRpacked, .extent = halfExt,
			.usage = Vulkan_ImageUsage::ComputeOnly, .mipLevels = 0, .bPerMipStorage = true,
			.debugName = "BloomMipchain" };
	}

	// --- Quarter resolution ---

	inline ImageDesc FlareBright(Extents3D quarterExt)
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = quarterExt,
				 .usage  = Vulkan_ImageUsage::ComputeOnly, .debugName = "FlareBright" };
	}

	inline ImageDesc LensFlareColor(Extents3D quarterExt)
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = quarterExt,
				 .usage  = Vulkan_ImageUsage::ComputeOnly, .debugName = "LensFlareColor" };
	}

	// --- Mip-chain targets ---

	inline ImageDesc HiZ(Extents3D ext, uint32_t mipCount)
	{
		return { .format = Vulkan_Format::R32U, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeOnly,
				 .mipLevels = mipCount, .bPerMipStorage = true, .debugName = "HiZ" };
	}

	inline ImageDesc LinearizedHiZ(Extents3D ext, uint32_t mipCount)
	{
		return { .format = Vulkan_Format::R32F, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeOnly,
				 .mipLevels = mipCount, .bPerMipStorage = true, .debugName = "LinearizedHiZ" };
	}

	// --- Shadow maps ---

	inline ImageDesc DirectionalCSMAtlas(Extents3D ext)
	{
		return { .format = Vulkan_Format::D32, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ShadowMap, .debugName = "DirectionalCSMAtlas" };
	}

	inline ImageDesc FlashlightShadowMap()
	{
		return { .format = Vulkan_Format::D32, .extent = { RD::FLASHLIGHT_SHADOW_MAP_X, RD::FLASHLIGHT_SHADOW_MAP_Y, 1 },
				 .usage  = Vulkan_ImageUsage::ShadowMap, .debugName = "FlashlightShadowMap" };
	}

	// 2 cascade atlas
	inline ImageDesc VolumetricShadowMap()
	{
		return { .format = Vulkan_Format::D16, .extent = { RD::VOL_SHADOW_MAP_X, RD::VOL_SHADOW_MAP_Y, 1 },
				 .usage = Vulkan_ImageUsage::ShadowMap, .debugName = "VolumetricShadowMap" };
	}

	inline ImageDesc ShadowInvalidMask(Extents3D eighthExt)
	{
		return { .format = Vulkan_Format::R8U, .extent = eighthExt,
				 .usage = Vulkan_ImageUsage::ComputeOnly, .debugName = "ShadowInvalidMask" };
	}

	// --- RT shadows (full resolution) ---

	inline ImageDesc RTShadowPenumbra(Extents3D ext)
	{
		return { .format = Vulkan_Format::R32F, .extent = ext,
				 .usage = Vulkan_ImageUsage::ComputeOnly, .debugName = "RTShadowPenumbra" };
	}

	inline ImageDesc RTShadowDenoised(Extents3D ext)
	{
		return { .format = Vulkan_Format::R16F, .extent = ext,
				 .usage = Vulkan_ImageUsage::ComputeOnly, .debugName = "RTShadowDenoised" };
	}

	inline ImageDesc NRDShadowNormalRoughness(Extents3D ext)
	{
		return { .format = Vulkan_Format::ABGRpacked, .extent = ext,
				 .usage = Vulkan_ImageUsage::ComputeOnly, .debugName = "NRDShadowNormalRoughness" };
	}

	inline ImageDesc NRDShadowViewZ(Extents3D ext)
	{
		return { .format = Vulkan_Format::R32F, .extent = ext,
				 .usage = Vulkan_ImageUsage::ComputeOnly, .debugName = "NRDShadowViewZ" };
	}


	inline ImageDesc FroxelScatterExt(uint32_t index)
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = { RD::FROXEL_GRID_X, RD::FROXEL_GRID_Y, RD::FROXEL_GRID_Z },
				 .usage = Vulkan_ImageUsage::ComputeOnly,
				 .imageType = VK_IMAGE_TYPE_3D,
				 .debugName = (index == 0) ? "FroxelScatterA" : "FroxelScatterB" };
	}

	inline ImageDesc FroxelIntegrated()
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = { RD::FROXEL_GRID_X, RD::FROXEL_GRID_Y, RD::FROXEL_GRID_Z },
				 .usage = Vulkan_ImageUsage::ComputeOnly,
				 .imageType = VK_IMAGE_TYPE_3D, .debugName = "FroxelIntegrated" };
	}

	inline ImageDesc SharpenedColor(Extents3D ext)
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = ext,
				 .usage = Vulkan_ImageUsage::ComputeRWTransfer, .debugName = "SharpenedColor" };
	}
}

namespace EnvironmentMapDescs
{
	inline ImageDesc Skybox()
	{
		return { .format = Vulkan_Format::BGRpacked,
				 .extent = { RD::SKYBOX_EXTENT, RD::SKYBOX_EXTENT, 1 },
				 .usage  = Vulkan_ImageUsage::ComputeRWTransfer, .mipLevels = 0,
				 .bIsCubemap = true, .debugName = "EnvSkybox" };
	}

	inline ImageDesc Specular(uint32_t mipCount)
	{
		return { .format = Vulkan_Format::RGBA16F,
				 .extent = { RD::SPECULAR_EXTENT, RD::SPECULAR_EXTENT, 1 },
				 .usage  = Vulkan_ImageUsage::ComputeOnly,
				 .mipLevels = mipCount, .bIsCubemap = true,
				 .bPerMipStorage = true, .debugName = "EnvSpecular" };
	}

	inline ImageDesc BRDFLut()
	{
		return { .format = Vulkan_Format::RG16F,
				 .extent = { RD::BRDF_EXTENT, RD::BRDF_EXTENT, 1 },
				 .usage  = Vulkan_ImageUsage::ComputeOnly, .debugName = "BRDFLut" };
	}
}

namespace StaticTextureDescs
{
	// 1x1 fallback / default textures

	inline ImageDesc White()
	{
		return { .format = Vulkan_Format::RGBA8unorm, .extent = { 1, 1, 1 },
				 .usage  = Vulkan_ImageUsage::TextureSampled, .debugName = "DefaultWhite" };
	}

	inline ImageDesc FlatNormal()
	{
		return { .format = Vulkan_Format::RGBA8unorm, .extent = { 1, 1, 1 },
				 .usage  = Vulkan_ImageUsage::TextureSampled, .debugName = "DefaultFlatNormal" };
	}

	inline ImageDesc DefaultMetalRough()
	{
		return { .format = Vulkan_Format::RG8unorm, .extent = { 1, 1, 1 },
				 .usage  = Vulkan_ImageUsage::TextureSampled, .debugName = "DefaultMetalRough" };
	}

	inline ImageDesc Dummy()
	{
		return { .format = Vulkan_Format::BGRpacked, .extent = { 1, 1, 1 },
				 .usage  = Vulkan_ImageUsage::TextureSampled, .debugName = "DummyBlack" };
	}

	inline ImageDesc DummyUint8()
	{
		return { .format = Vulkan_Format::R8U, .extent = { 1, 1, 1 },
				 .usage  = Vulkan_ImageUsage::TextureSampled, .debugName = "DummyUint8" };
	}

	inline ImageDesc DummyVelocity()
	{
		return { .format = Vulkan_Format::RG16F, .extent = { 1, 1, 1 },
				 .usage = Vulkan_ImageUsage::ComputeRWTransfer, .debugName = "DummyVelocity" };
	}

	inline ImageDesc ErrorCheckerboard()
	{
		return { .format = Vulkan_Format::RGBA8unorm, .extent = { 16, 16, 1 },
				 .usage  = Vulkan_ImageUsage::TextureSampled, .debugName = "ErrorCheckerboard" };
	}

	// LUT textures

	inline ImageDesc RainbowLUT()
	{
		return { .format = Vulkan_Format::RGBA8unorm, .extent = { 256, 1, 1 },
				 .usage  = Vulkan_ImageUsage::TextureSampled, .debugName = "RainbowLUT" };
	}

	inline ImageDesc HilbertCurveLUT()
	{
		return { .format = Vulkan_Format::R16U, .extent = { 64, 64, 1 },
				 .usage  = Vulkan_ImageUsage::TextureSampled, .debugName = "HilbertCurveLUT" };
	}

	// SMAA precomputed textures (fixed size from the SMAA header)

	inline ImageDesc SMAAArea()
	{
		return { .format = Vulkan_Format::RG8unorm, .extent = { 160, 560, 1 },
				 .usage  = Vulkan_ImageUsage::TextureSampled, .debugName = "SMAAArea" };
	}

	inline ImageDesc SMAASearch()
	{
		return { .format = Vulkan_Format::R8unorm, .extent = { 64, 16, 1 },
				 .usage  = Vulkan_ImageUsage::TextureSampled, .debugName = "SMAASearch" };
	}

	// File-loaded textures

	inline ImageDesc CookieGobo()
	{
		return { .format = Vulkan_Format::R8unorm, .extent = { 512, 512, 1 },
				 .usage  = Vulkan_ImageUsage::TextureSampled, .debugName = "CookieGobo" };
	}
}
