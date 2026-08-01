#pragma once

#include "EngineTypes.h"
#include "../VulkanTypes.h"

// TODO: Add more predefined extents in this header
#include "../../RendererDefinitions.h"
namespace RD = RendererDefinitions;

namespace RenderTargetDescs
{
	// --- Full resolution matches swapchain ---

	inline ImageDesc Opaque(Extents3D ext)
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = ext,
				 .usage  = Vulkan_ImageUsage::DrawColor, .debugName = "Opaque" };
	}

	inline ImageDesc TransparentResolved(Extents3D ext)
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeOnly, .debugName = "TransparentResolved" };
	}

	inline ImageDesc TransparentAccumulation(Extents3D ext)
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = ext,
				 .usage  = Vulkan_ImageUsage::DrawColor, .debugName = "TransparentAccumulation" };
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

	inline ImageDesc DepthRaw(Extents3D ext)
	{
		return { .format = Vulkan_Format::D32, .extent = ext,
				 .usage  = Vulkan_ImageUsage::DrawDepth, .debugName = "DepthRaw" };
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

	inline ImageDesc PrevVelocity(Extents3D ext)
	{
		return { .format = Vulkan_Format::RG16F, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeReadWrite, .debugName = "PrevVelocity" };
	}

	inline ImageDesc Visibility(Extents3D ext)
	{
		return { .format = Vulkan_Format::RG32U, .extent = ext,
				 .usage  = Vulkan_ImageUsage::DrawColor, .debugName = "Visibility" };
	}

	inline ImageDesc ViewSpaceNormals(Extents3D ext)
	{
		return { .format = Vulkan_Format::RG8unorm, .extent = ext,
				 .usage  = Vulkan_ImageUsage::DrawColor, .debugName = "ViewSpaceNormals" };
	}

	inline ImageDesc ToneMap(Extents3D ext)
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeReadWrite, .debugName = "ToneMap" };
	}

	inline ImageDesc AOEdgeInfo(Extents3D ext)
	{
		return { .format = Vulkan_Format::R8unorm, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeOnly, .debugName = "AOEdgeInfo" };
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

	inline ImageDesc BentNormals(Extents3D ext)
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeReadWrite, .debugName = "BentNormals" };
	}

	inline ImageDesc ColorHistory(Extents3D ext, uint32_t index)
	{
		// index disambiguates the two ping-pong slots in debug tools
		return { .format = Vulkan_Format::RGBA16F, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeReadWrite,
				 .debugName = (index == 0) ? "ColorHistoryA" : "ColorHistoryB" };
	}

	inline ImageDesc AAColor(Extents3D ext)
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeReadWrite, .debugName = "AAColor" };
	}

	inline ImageDesc MaterialAlbedoRough(Extents3D ext)
	{
		return { .format = Vulkan_Format::RGBA8unorm, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeReadWrite, .debugName = "MaterialAlbedoRough" };
	}

	inline ImageDesc MaterialNormal(Extents3D ext)
	{
		return { .format = Vulkan_Format::RG16unorm, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeReadWrite, .debugName = "MaterialNormal" };
	}

	inline ImageDesc MaterialMetal(Extents3D ext)
	{
		return { .format = Vulkan_Format::R8unorm, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeReadWrite, .debugName = "MaterialMetal" };
	}

	inline ImageDesc MaterialEmissive(Extents3D ext)
	{
		return { .format = Vulkan_Format::BGRpacked, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeReadWrite, .debugName = "MaterialEmissive" };
	}

	inline ImageDesc PostNonAAComposite(Extents3D ext)
	{
		return { .format = Vulkan_Format::RGBA16F, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeReadWrite, .debugName = "PostNonAAComposite" };
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

	// --- Half resolution ---

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

	// CMAA2 uses { halfWidth, fullHeight }
	inline ImageDesc CMAA2WorkingEdges(Extents3D ext)
	{
		return { .format = Vulkan_Format::R8U, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeReadWrite, .debugName = "CMAA2WorkingEdges" };
	}

	inline ImageDesc BloomMipchain(Extents3D halfExt)
	{
		return { .format = Vulkan_Format::BGRpacked, .extent = halfExt,
			.usage = Vulkan_ImageUsage::ComputeReadWrite, .mipLevels = 0, .bPerMipStorage = true,
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

	inline ImageDesc LinearizedMinHiZ(Extents3D ext, uint32_t mipCount)
	{
		return { .format = Vulkan_Format::R32F, .extent = ext,
				 .usage  = Vulkan_ImageUsage::ComputeOnly,
				 .mipLevels = mipCount, .bPerMipStorage = true, .debugName = "LinearizedMinHiZ" };
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
}

namespace EnvironmentMapDescs
{
	inline ImageDesc Skybox()
	{
		return { .format = Vulkan_Format::BGRpacked,
				 .extent = { 512, 512, 1 },
				 .usage  = Vulkan_ImageUsage::ComputeReadWrite,
				 .bIsCubemap = true, .debugName = "EnvSkybox" };
	}

	inline ImageDesc Specular(uint32_t mipCount)
	{
		return { .format = Vulkan_Format::RGBA16F,
				 .extent = { 256, 256, 1 },
				 .usage  = Vulkan_ImageUsage::ComputeOnly,
				 .mipLevels = mipCount, .bIsCubemap = true,
				 .bPerMipStorage = true, .debugName = "EnvSpecular" };
	}

	inline ImageDesc Irradiance()
	{
		return { .format = Vulkan_Format::RGBA16F,
				 .extent = { 32, 32, 1 },
				 .usage  = Vulkan_ImageUsage::ComputeOnly,
				 .bIsCubemap = true, .debugName = "EnvIrradiance" };
	}

	inline ImageDesc BRDFLut()
	{
		return { .format = Vulkan_Format::RG16F,
				 .extent = { 512, 512, 1 },
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

	inline ImageDesc BlackEmissive()
	{
		return { .format = Vulkan_Format::RGBA8unorm, .extent = { 1, 1, 1 },
				 .usage  = Vulkan_ImageUsage::TextureSampled, .debugName = "DefaultEmissive" };
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
