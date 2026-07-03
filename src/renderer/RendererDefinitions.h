#pragma once

#include <cstdint>

namespace RendererDefinitions
{
	// -------------
	// GPU Bindings
	// -------------
	// Descriptor info
	inline constexpr uint32_t GLOBAL_SET = 0u;
	inline constexpr uint32_t FRAME_SET  = 1u;
	inline constexpr uint32_t PUSH_SET   = 2u;

	// Shared between global and frame
	// ALL ssbos come from this table
	inline constexpr uint32_t ADDRESS_TABLE_BINDING           = 0u;

	// Global bindings
	inline constexpr uint32_t GLOBAL_BINDING_DEBUG_INLINE     = 1u;
	inline constexpr uint32_t GLOBAL_BINDING_SAMPLER_CUBE     = 2u;
	inline constexpr uint32_t GLOBAL_BINDING_COMBINED_SAMPLER = 3u;

	// Frame bindings
	inline constexpr uint32_t FRAME_BINDING_SCENE     = 1u;
	inline constexpr uint32_t FRAME_BINDING_CSM       = 2u;
	inline constexpr uint32_t FRAME_BINDING_CLUSTERED = 3u;

	// Push bindings
	inline constexpr uint32_t PUSH_BINDING_READ_1  = 0u;
	inline constexpr uint32_t PUSH_BINDING_READ_2  = 1u;
	inline constexpr uint32_t PUSH_BINDING_READ_3  = 2u;
	inline constexpr uint32_t PUSH_BINDING_READ_4  = 3u;
	inline constexpr uint32_t PUSH_BINDING_READ_5  = 4u;
	inline constexpr uint32_t PUSH_BINDING_READ_6  = 5u;
	inline constexpr uint32_t PUSH_BINDING_READ_7  = 6u;
	inline constexpr uint32_t PUSH_BINDING_WRITE_1 = 7u;
	inline constexpr uint32_t PUSH_BINDING_WRITE_2 = 8u;
	inline constexpr uint32_t PUSH_BINDING_WRITE_3 = 9u;
	inline constexpr uint32_t PUSH_BINDING_WRITE_4 = 10u;
	inline constexpr uint32_t PUSH_BINDING_WRITE_5 = 11u;

	inline constexpr uint32_t VERTS_LINE_COUNT         = 24u;
	inline constexpr size_t DISPATCH_SLOT_STRIDE_BYTES = 16u;

	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_LIGHTS         = 0u;  // args[0]
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_CLUSTERS       = 1u;  // args[1]
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_CMAA2_SHAPES   = 2u;  // args[2]
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_CMAA2_DEFERRED = 3u;  // args[3]

	// Lights or clusters?
	inline constexpr uint64_t DISPATCH_LIGHTS_OFFSET_BYTES         = INDIRECT_DISPATCH_SLOT_LIGHTS         * DISPATCH_SLOT_STRIDE_BYTES;
	// Cluster count info
	inline constexpr uint64_t DISPATCH_CLUSTERS_OFFSET_BYTES       = INDIRECT_DISPATCH_SLOT_CLUSTERS       * DISPATCH_SLOT_STRIDE_BYTES;

	inline constexpr uint64_t DISPATCH_CMAA2_SHAPES_OFFSET_BYTES   = INDIRECT_DISPATCH_SLOT_CMAA2_SHAPES   * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_CMAA2_DEFERRED_OFFSET_BYTES = INDIRECT_DISPATCH_SLOT_CMAA2_DEFERRED * DISPATCH_SLOT_STRIDE_BYTES;

	// -------------------
	// Renderer constants
	// -------------------
	inline constexpr uint32_t MAX_FRAME_INSTANCES_TOTAL     = 262144u;
	inline constexpr uint32_t MAX_FRAME_DRAW_COMMANDS_TOTAL = 65536u;
	inline constexpr uint32_t MAX_INSTANCE_TRANSFORMS       = 200000u;
	inline constexpr uint32_t MAX_FRAMES_IN_FLIGHT          = 3u;
	inline constexpr uint32_t MAX_LIGHTS                    = 4096u; // standard
	inline constexpr uint32_t MAX_PUSH_CONSTANT_SIZE        = 128u;

	// Static lights in global list
	inline constexpr uint32_t LIGHT_LIST_STATIC_COUNT     = 1u;
	//inline constexpr uint32_t LIGHT_LIST_SLOT_DIRECTIONAL = 0u;
	inline constexpr uint32_t LIGHT_LIST_SLOT_FLASHLIGHT  = 0u;

	inline constexpr uint32_t LIGHT_FLAG_CASTS_SPOT_SHADOW  = 1u << 0;
	inline constexpr uint32_t LIGHT_FLAG_FLASHLIGHT         = 1u << 1;
	inline constexpr uint32_t LIGHT_FLAG_FLASHLIGHT_OFF     = 1u << 2;
	inline constexpr uint32_t CLUSTERS_TILE_SLICE_X         = 32u;
	inline constexpr uint32_t CLUSTERS_TILE_SLICE_Y         = 32u;
	inline constexpr uint32_t CLUSTERS_TILE_SLICE_Z         = 24u;
	//inline constexpr size_t MAX_VISIBLE_LIGHT_ID_GPU_BYTES = RD::MAX_LIGHTS * sizeof(uint32_t);
	inline constexpr uint32_t MAX_LIGHTS_PER_CLUSTER        = 512u;
	inline constexpr uint32_t MAX_VISIBLE_LIGHTS            = MAX_LIGHTS - LIGHT_LIST_STATIC_COUNT;

	inline constexpr float ANISOTROPY_LEVEL_16        = 16.0f;
	inline constexpr float ANISOTROPY_LEVEL_8         = 8.0f;
	inline constexpr float ANISOTROPY_LEVEL_4         = 4.0f;
	inline constexpr float ANISOTROPY_LEVEL_2         = 2.0f;


	// FPS targets
	inline constexpr float TARGET_FPS_60       = 60.0f;
	inline constexpr float TARGET_FPS_90       = 90.0f;
	inline constexpr float TARGET_FPS_100      = 100.0f;
	inline constexpr float TARGET_FPS_120      = 120.0f;
	inline constexpr float TARGET_FPS_144      = 144.0f;
	inline constexpr float TARGET_FPS_165      = 165.0f;
	inline constexpr float TARGET_FPS_180      = 180.0f;
	inline constexpr float TARGET_FPS_200      = 200.0f;
	inline constexpr float TARGET_FPS_240      = 240.0f;
	inline constexpr float TARGET_FPS_300      = 300.0f;
	inline constexpr float TARGET_FPS_360      = 360.0f;
	inline constexpr float TARGET_FPS_480      = 480.0f;

	// Resource Limits
	inline constexpr uint32_t MAX_MIP_LEVELS          = 12u;
	inline constexpr uint32_t MAX_SHADOW_CASCADES     = 4u;
	inline constexpr uint32_t MAX_LUMINANCE_GROUPS    = 65536u;
	inline constexpr uint32_t HI_Z_MIP_COUNT          = 5u;

	inline constexpr uint32_t MAX_ENVIRONMENT_SETS            = 8u;  // 128 uniform alignment
	inline constexpr uint32_t SPECULAR_PREFILTERED_MIP_LEVELS = 9;
	inline constexpr float DIFFUSE_SAMPLE_DELTA               = 0.025f;
	inline constexpr uint32_t PREFILTER_SAMPLE_COUNT          = 2048;

	// Image array sizes
	inline constexpr uint32_t MAX_SAMPLER_CUBE_IMAGES      = 100u;
	inline constexpr uint32_t MAX_COMBINED_SAMPLERS_IMAGES = 10000u;

	inline constexpr uint32_t CSM_QUALITY_ULTRA  = 8192u;
	inline constexpr uint32_t CSM_QUALITY_HIGH   = 4096u;
	inline constexpr uint32_t CSM_QUALITY_MEDIUM = 3072u;
	inline constexpr uint32_t CSM_QUALITY_LOW    = 2048u;

	inline constexpr uint32_t FLASHLIGHT_SHADOW_QUALITY_HIGH = 512u;
	//inline constexpr uint32_t FLASHLIGHT_SHADOW_QUALITY_LOW  = 256u;

	inline constexpr uint32_t FLASHLIGHT_SHADOW_MAP_X = FLASHLIGHT_SHADOW_QUALITY_HIGH;
	inline constexpr uint32_t FLASHLIGHT_SHADOW_MAP_Y = FLASHLIGHT_SHADOW_QUALITY_HIGH;

	enum class ShadowQuality
	{
		Low,
		Medium,
		High,
		Ultra,
	};

	inline uint32_t EvaluateShadowQuality(ShadowQuality quality)
	{
		switch(quality)
		{
			case ShadowQuality::Low:    return CSM_QUALITY_LOW;
			case ShadowQuality::Medium: return CSM_QUALITY_MEDIUM;
			case ShadowQuality::High:   return CSM_QUALITY_HIGH;
			case ShadowQuality::Ultra:  return CSM_QUALITY_ULTRA;
		}
	}

	enum class Renderer_Pass
	{
		Prepass,
		HiZGeneration,
		LightCulling,
		ClusteredLights,
		SSAO,
		DirectionalCSMAtlas,
		FlashlightShadow,
		ScreenSpaceContactShadows,
		Skybox,
		OpaqueForward,
		OBBLineView,
		TransparentForward,
		TransparentResolve,
		VolumetricLighting,
		TAA,
		LuminanceExposure,
		Bloom,
		LensFlare,
		FinalComposite,
		CMAA2,
		SMAA,
		FXAA,
		ChromaticAberration,

		Count
	};

	struct PassTimestampRange
	{
		uint32_t beginQuery = UINT32_MAX;
		uint32_t endQuery = UINT32_MAX;
	};

	inline constexpr size_t PASS_COUNT = static_cast<size_t>(Renderer_Pass::Count);

	enum class Renderer_Shader
	{
		Opaque_v,
		Opaque_f,
		Transparent_f,
		TransparentResolve_c,
		Skybox_v,
		Skybox_f,

		Wireframe_v,
		Wireframe_f,
		ObbLine_v,
		ObbLine_f,

		//Visibility_c,

		ExposureReduce_c,
		ExposureFinalize_c,
		FinalComposite_c,

		HDRToCubemap_c,
		SpecularPrefilter_c,
		DiffuseIrradiance_c,
		BRDFLUT_c,

		Prepass_v,
		Prepass_f,
		Shadow_v,
		HiZGen_c,

		SSAO_c,
		SSAOFilter_c,
		SSAODenoise_c,
		SSAODepthPrefilter_c,

		VolumetricLight_c,
		VolumetricLightBlur_c,

		FlareBright_c,
		FlareGen_c,

		BloomDownsample_c,
		BloomUpsample_c,

		LightCulling_c,
		ClusterTileSliceRanges_c,
		IndirectArgsLight_c,
		ClusterCount_c,
		ClusterScanOffsets_c,
		ClusterScatterIDs_c,

		SMAAEdges_c,
		SMAAWeights_c,
		SMAABlend_c,

		CMAA2Edges_c,
		CMAA2ShapeCandidates_c,
		CMAA2DeferredResolve_c,
		CMAA2DispatchArgs_c,

		FXAA_c,
		TAA_c,

		ScreenSpaceContactShadows_c,

		ChromaticAberration_c,

		Count
	};

	inline constexpr size_t SHADER_COUNT = static_cast<size_t>(Renderer_Shader::Count);

	enum class Renderer_Pipeline
	{
		Opaque,
		Transparent,
		TransparentResolve,
		Skybox,

		Wireframe,
		OBBLine,

		//Visibility,

		ExposureReduce,
		ExposureFinalize,
		FinalComposite,

		HDRToCubemap,
		SpecularPrefilter,
		DiffuseIrradiance,

		BRDFLUT,

		Prepass,
		Shadow,
		HiZGen,

		SSAODepthPrefilter,
		SSAO,
		SSAOFilter,
		SSAODenoise,

		VolumetricLight,
		VolumetricLightBlur,

		FlareBright,
		FlareGen,

		BloomDownsample,
		BloomUpsample,

		LightCulling,

		ClusterTileSliceRanges,
		IndirectArgsLight,
		ClusterCount,
		ClusterScanOffsets,
		ClusterScatterIDs,

		SMAAEdges,
		SMAAWeights,
		SMAABlend,

		CMAA2Edges,
		CMAA2ShapeCandidates,
		CMAA2DeferredResolve,
		CMAA2DispatchArgs,

		FXAA,
		TAA,

		ScreenSpaceContactShadows,

		ChromaticAberration,

		Count
	};

	inline constexpr size_t PIPELINE_COUNT = static_cast<size_t>(Renderer_Pipeline::Count);

	enum class Renderer_RenderTarget
	{
		Opaque,
		TransparentResolved,
		TransparentAccumulation,
		TransparentRevealage,
		Tonemap,
		DepthResolved,
		PrevDepthResolved,
		DepthRaw,
		HiZ,
		LinearizedMinHiZ,
		AORaw,
		AOTemp,
		AoEdgeInfo,
		BentNormals,
		AAColor,
		ColorHistoryA,
		ColorHistoryB,
		FlareBright,
		LensFlareColor,
		BloomMipchain,
		Velocity,
		PrevVelocity,
		ViewSpaceNormals,
		VolumetricLight,
		VolumetricLightBlur,
		PostNonAAComposite,
		CMAA2WorkingEdges,
		SMAAEdges,
		SMAAWeights,
		SSContactShadows,
		DirectionalCSMAtlas,
		FlashlightShadowMap,
		Count
	};

	inline constexpr size_t RENDER_TARGET_COUNT = static_cast<size_t>(Renderer_RenderTarget::Count);

	enum class Renderer_Texture
	{
		RainbowLut,
		CookieGobo,
		SMAAArea,
		SMAASearch,
		HilbertCurveLut,
		Dummy,
		DummyU8,
		Brdf,
		MetalRough,
		White,
		Emissive,
		Normal,
		Checkerboard,

		Count
	};

	inline constexpr size_t STATIC_TEXTURE_COUNT = static_cast<size_t>(Renderer_Texture::Count);

	enum class Renderer_Sampler
	{
		NearestClamp,
		LinearClamp,
		HiZ,
		LinearLodClamp,
		PointBorder,
		TaaHistory,
		Noise,
		ShadowMap,
		Linear,
		Nearest,
		Brdf,
		Specular,
		Irradiance,
		Skybox,
		Count
	};

	inline constexpr size_t SAMPLER_COUNT = static_cast<size_t>(Renderer_Sampler::Count);
	
	// ssbo buffers inside the bindless address table
	enum class Renderer_Buffer
	{
	// Frame context owned
		VisibleInstances,
		IndirectDraws,

		VisibleLightCount,
		VisibleLightIDs,

		ClusterCounts,
		ClusterOffsets,
		ClusterCursors,
		ClusterLightIDs,
		ClusterTileSliceRanges,
		ClusterScanScratch,

		Cmaa2Control,
		Cmaa2ShapeCandidates,
		Cmaa2DeferredLocations,
		Cmaa2DeferredItems,
		Cmaa2DeferredHeads,

		DispatchIndirectArgs,

		Lights,
		Transforms,
		PrevTransforms,

	// Global access
		Material,
		Mesh,
		Vertex,
		Index,
		Luminance,

		Count
	};

	inline constexpr size_t ADDRESS_TABLE_BUFFER_COUNT = static_cast<size_t>(Renderer_Buffer::Count);

	// Instance drawing counts and transforms
	enum class InstancingMethod
	{
		DrawStatic,      // single baked instance
		DrawMultiStatic, // many baked instances
		DrawDynamic,     // single instance, dynamic transform
		DrawMultiDynamic // many instances, dynamic transforms
	};

	enum class AmbientOcclusionMethod
	{
		AO_OFF,
		AO_GTAO, // Ground Truth Ambient Occlusion
		AO_GTAO_BENT_NORMALS
	};

	enum class AntiAliasingMethod
	{
		AA_OFF,
		AA_CMAA2,   // Conservative Morphological Anti-Aliasing 2
		AA_SMAA,    // Sub-Pixel Morphological Anti-Aliasing
		AA_FXAA,    // Fast Approximate Anti-Aliasing
		AA_TAA      // Temporal Anti-Aliasing
	};

	//enum class ToneMapper
	//{
	//	TM_ACESFILM,
	//	TM_GT7
	//};

	// Define this somewhere
	struct alignas(4) RenderToggles
	{
		uint32_t enableOBBs                = 0;
		uint32_t enableLensFlare           = 0;
		uint32_t enableChromaticAberration = 0;
		uint32_t enableSSS                 = 0;

		uint32_t aaMode                    = 0;
		uint32_t aoMode                    = 0;
		uint32_t enableShadows             = 0;
		uint32_t enableVolumetrics         = 0;

		uint32_t showAlbedo                = 0;
		uint32_t showNormals               = 0;
		uint32_t showRoughness             = 0;
		uint32_t showMetallic              = 0;

		uint32_t showAmbientOcclusion      = 0;
		uint32_t showSpecular              = 0;
		uint32_t showDiffuse               = 0;
		uint32_t showEmissive              = 0;

		uint32_t showBentNormals           = 0;
		uint32_t showCascadeSplits         = 0;
		uint32_t showSSS                   = 0;
		uint32_t activeEnvMap              = 0;

		uint32_t enableProfilerView        = 0;
		uint32_t enableSettings            = 0;
		uint32_t enableBloom               = 0;
		float bloomIntensity               = 0.0;
	};

	enum class ImageAccess
	{
		Undefined,

		TransferSrc,
		TransferDst,

		Read,
		Write,

		GraphicsColorWrite,
		GraphicsDepthWrite,

		Present,

		DepthRead
	};

	enum class BufferAccess
	{
		Undefined,

		TransferWrite,
		TransferRead,

		Read,
		ComputeWrite,
		ComputeReadWrite,

		VertexRead,
		IndexRead,
		IndirectRead,

		FragmentRead,
		GraphicsRead
	};

	enum class ResourceLifetime
	{
		Persistent,     // engine lifetime, freed at shutdown only
		FrameVolatile,  // freed after N frames-in-flight
		RenderTarget,   // freed and rebuilt on resize/setting change
		Asset           // freed when scene or asset owner dies
	};

	enum class DescriptorSlot
	{
		Unified,
		Frame,
		Push,
		Count
	};

	struct RenderStateInfo
	{
		bool bIsOpaqueVisible      = false;
		bool bIsTransparentVisible = false;
		bool bHasVisibles          = bIsOpaqueVisible || bIsTransparentVisible;
		bool bTemporalValid        = false;
		bool bStateChanged         = false;
		bool bFlashlightOn         = false;
		bool bCopyPostAAImage      = false;
		bool bShowImgui            = false;
		uint32_t activeLightCount = 0u;
	};
}
