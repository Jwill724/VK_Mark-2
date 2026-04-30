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
	inline constexpr uint32_t ADDRESS_TABLE_BINDING           = 0u;

	// Global bindings
	inline constexpr uint32_t GLOBAL_BINDING_ENV_INDEX        = 1u;
	inline constexpr uint32_t GLOBAL_BINDING_DEBUG_INLINE     = 2u;
	inline constexpr uint32_t GLOBAL_BINDING_SAMPLER_CUBE     = 3u;
	inline constexpr uint32_t GLOBAL_BINDING_COMBINED_SAMPLER = 4u;

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

	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_LIGHTS         = 0u;  // args[0]
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_CLUSTERS       = 1u;  // args[1]
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_CMAA2_SHAPES   = 2u;  // args[2]
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_CMAA2_DEFERRED = 3u;  // args[3]

	// -------------------
	// Renderer constants
	// -------------------
	inline constexpr uint32_t MAX_FRAME_INSTANCES_TOTAL     = 262144u;
	inline constexpr uint32_t MAX_FRAME_DRAW_COMMANDS_TOTAL = 65536u;
	inline constexpr uint32_t MAX_INSTANCE_TRANSFORMS       = 200000u;
	inline constexpr uint32_t MAX_FRAMES_IN_FLIGHT          = 3u;
	inline constexpr uint32_t MAX_LIGHTS                    = 4096u; // standard
	inline constexpr uint32_t MAX_PUSH_CONSTANT_SIZE        = 128u;
	inline constexpr uint32_t LIGHT_FLAG_CASTS_SPOT_SHADOW  = 1u << 0;
	inline constexpr uint32_t LIGHT_FLAG_FLASHLIGHT         = 1u << 1;
	inline constexpr uint32_t LIGHT_FLAG_FLASHLIGHT_OFF     = 1u << 2;

	// Static lights in global list
	inline constexpr uint32_t LIGHT_LIST_STATIC_COUNT    = 1u;
	inline constexpr uint32_t LIGHT_LIST_SLOT_FLASHLIGHT = 0u;

	inline constexpr float ANISOTROPY_LEVEL_16        = 16.0f;
	inline constexpr float ANISOTROPY_LEVEL_8         = 8.0f;
	inline constexpr float ANISOTROPY_LEVEL_4         = 4.0f;
	inline constexpr float ANISOTROPY_LEVEL_2         = 2.0f;

	// Resource Limits
	inline constexpr uint32_t MAX_MIP_LEVELS          = 12u;
	inline constexpr uint32_t MAX_ENV_SETS            = 8u;  // 128 uniform alignment
	inline constexpr uint32_t MAX_SHADOW_CASCADES     = 4u;
	inline constexpr uint32_t MAX_LUMINANCE_GROUPS    = 65536u;
	inline constexpr uint32_t HI_Z_MIP_COUNT          = 5u;

	// Image array sizes
	inline constexpr uint32_t MAX_SAMPLER_CUBE_IMAGES      = 100u;
	inline constexpr uint32_t MAX_COMBINED_SAMPLERS_IMAGES = 10000u;

	inline constexpr uint32_t VERTS_LINE_COUNT         = 24u;
	inline constexpr size_t DISPATCH_SLOT_STRIDE_BYTES = 16u;

	// Defined in order of execution in pipeline
	enum class Renderer_Pass
	{
		Prepass,
		HiZGeneration,
		ClusteredLightBuild,
		SSAO,
		DirectionalCSM,
		FlashlightShadow,
		ScreenSpaceContactShadows,
		Skybox,
		OpaqueForward,
		OBBLineView,
		TransparentForward,
		TransparentResolve,
		VolumetricLighting,
		TAA,
		Exposure,
		LensFlare,
		FinalComposite,
		CMAA2,
		SMAA,
		FXAA,
		ChromaticAberration,

		None
	};

	enum class Renderer_Shader
	{
		Opaque_v,
		Opaque_f,
		Transparent_v,
		Transparent_f,
		TransparentResolve_f,
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

		ClusterTileSliceRanges_c,
		VisibleLightList_c,
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

		SSAO,
		SSAOFilter,
		SSAODenoise,
		SSAODepthPrefilter,

		VolumetricLight,
		VolumetricLightBlur,

		FlareBright,
		FlareGen,

		ClusterTileSliceRanges,
		VisibleLightList,
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
		LinearizedHiZ,
		AORaw,
		AOTemp,
		AoEdgeInfo,
		BentNormals,
		AAColor,
		ColorHistoryA,
		ColorHistoryB,
		FlareBright,
		LensFlareColor,
		Velocity,
		PrevVelocity,
		ViewSpaceNormals,
		VolumetricLight,
		VolumetricLightBlur,
		DirectionalCSM,
		SSContactShadows,
		FlashlightShadowMap,
		PostNonAAComposite,
		CMAA2WorkingEdges,
		SMAAEdges,
		SMAAWeights,
		Count
	};

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

	
	// ssbo buffers inside the bindless address table
	enum class Renderer_Buffer
	{
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
		Material,
		Mesh,
		Vertex,
		Index,
		Luminance,

		Count
	};

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

	enum class ToneMapper
	{
		TM_ACESFILM,
		TM_GT7
	};

	enum class ShadowFilter
	{
		SHADOW_FILTER_PCF,
		//SHADOW_FILTER_PCSS
	};

	// Define this somewhere
	struct alignas(4) RenderToggles
	{
		uint32_t enableOBBs                = 0;
		uint32_t enableLensFlare           = 0;
		uint32_t enableChromaticAberration = 0;
		uint32_t enableSSS                 = 0;

		uint32_t aaMode                    = 0;
		uint32_t aoMode                    = 0;
		uint32_t shadowFilter              = 0;
		uint32_t enableShadows             = 0;

		uint32_t enableVolumetrics         = 0;
		uint32_t tonemapper                = 0;
		uint32_t activeEnvMap              = 0;
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

		uint32_t enableProfilerView        = 0;
		uint32_t enableSettings            = 0;
	};

	enum class ResourceAccess
	{
		Read,
		Write,
		Read_Write,
		Write_Read,
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
}
