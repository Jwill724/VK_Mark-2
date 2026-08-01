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
	inline constexpr uint32_t PUSH_BINDING_READ_8  = 7u;
	inline constexpr uint32_t PUSH_BINDING_READ_9  = 8u;
	inline constexpr uint32_t PUSH_BINDING_WRITE_1 = 9u;
	inline constexpr uint32_t PUSH_BINDING_WRITE_2 = 10u;
	inline constexpr uint32_t PUSH_BINDING_WRITE_3 = 11u;
	inline constexpr uint32_t PUSH_BINDING_WRITE_4 = 12u;
	inline constexpr uint32_t PUSH_BINDING_WRITE_5 = 13u;

	// -------------------------
	// Indirect Dispatch Slots
	// -------------------------

	inline constexpr size_t DISPATCH_SLOT_STRIDE_BYTES      = 16u; // uvec4

	// Draw-build pipeline — stream dispatch args
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_STREAM_OPAQUE        = 0u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_STREAM_OPAQUE_MASKED = 1u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_STREAM_TRANSPARENT   = 2u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_STREAM_FLASHLIGHT    = 3u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_STREAM_CSM0          = 4u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_STREAM_CSM1          = 5u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_STREAM_CSM2          = 6u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_STREAM_CSM3          = 7u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_SCATTER              = 8u;  // total-visible dispatch

	// Other systems
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_DEBUG_BUILD          = 9u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_LIGHTS               = 10u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_CLUSTERS             = 11u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_CMAA2_SHAPES         = 12u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_CMAA2_DEFERRED       = 13u;
	inline constexpr uint32_t INDIRECT_DISPATCH_SLOT_COUNT                = 14u;

	// Byte offsets — multiply slot by stride
	inline constexpr uint64_t DISPATCH_STREAM_OPAQUE_BYTES            = INDIRECT_DISPATCH_SLOT_STREAM_OPAQUE        * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_STREAM_OPAQUE_MASKED_BYTES     = INDIRECT_DISPATCH_SLOT_STREAM_OPAQUE_MASKED * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_STREAM_TRANSPARENT_BYTES       = INDIRECT_DISPATCH_SLOT_STREAM_TRANSPARENT   * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_STREAM_FLASHLIGHT_OFFSET_BYTES = INDIRECT_DISPATCH_SLOT_STREAM_FLASHLIGHT    * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_STREAM_CSM0_OFFSET_BYTES       = INDIRECT_DISPATCH_SLOT_STREAM_CSM0          * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_STREAM_CSM1_OFFSET_BYTES       = INDIRECT_DISPATCH_SLOT_STREAM_CSM1          * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_STREAM_CSM2_OFFSET_BYTES       = INDIRECT_DISPATCH_SLOT_STREAM_CSM2          * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_STREAM_CSM3_OFFSET_BYTES       = INDIRECT_DISPATCH_SLOT_STREAM_CSM3          * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_SCATTER_OFFSET_BYTES           = INDIRECT_DISPATCH_SLOT_SCATTER              * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_DEBUG_BUILD_OFFSET_BYTES       = INDIRECT_DISPATCH_SLOT_DEBUG_BUILD          * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_LIGHTS_OFFSET_BYTES            = INDIRECT_DISPATCH_SLOT_LIGHTS               * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_CLUSTERS_OFFSET_BYTES          = INDIRECT_DISPATCH_SLOT_CLUSTERS             * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_CMAA2_SHAPES_OFFSET_BYTES      = INDIRECT_DISPATCH_SLOT_CMAA2_SHAPES         * DISPATCH_SLOT_STRIDE_BYTES;
	inline constexpr uint64_t DISPATCH_CMAA2_DEFERRED_OFFSET_BYTES    = INDIRECT_DISPATCH_SLOT_CMAA2_DEFERRED       * DISPATCH_SLOT_STRIDE_BYTES;

	// -------------------------
	// Visibility Stream Slots
	// -------------------------

	// Used during instance cull to fill indirect dispatch
	inline constexpr uint32_t VIS_SLOT_OPAQUE        = 0u;
	inline constexpr uint32_t VIS_SLOT_OPAQUE_MASKED = 1u;
	inline constexpr uint32_t VIS_SLOT_TRANSPARENT   = 2u;
	inline constexpr uint32_t VIS_SLOT_FLASHLIGHT    = 3u;
	inline constexpr uint32_t VIS_SLOT_CSM0          = 4u;
	inline constexpr uint32_t VIS_SLOT_CSM1          = 5u;
	inline constexpr uint32_t VIS_SLOT_CSM2          = 6u;
	inline constexpr uint32_t VIS_SLOT_CSM3          = 7u;
	inline constexpr uint32_t VIS_SLOT_COUNT         = 8u;

	// Used to build draws
	inline constexpr uint32_t VIS_PRIMARY_OPAQUE         = 1u << 0;
	inline constexpr uint32_t VIS_PRIMARY_OPAQUE_MASKED  = 1u << 1;
	inline constexpr uint32_t VIS_PRIMARY_TRANSPARENT    = 1u << 2;
	inline constexpr uint32_t VIS_FLASHLIGHT             = 1u << 3;
	inline constexpr uint32_t VIS_CSM0                   = 1u << 4;
	inline constexpr uint32_t VIS_CSM1                   = 1u << 5;
	inline constexpr uint32_t VIS_CSM2                   = 1u << 6;
	inline constexpr uint32_t VIS_CSM3                   = 1u << 7;

	// -------------------------
	// Draw Region Offsets
	// -------------------------
	inline constexpr uint32_t INDIRECT_CMD_SIZE      = 20u; // VkDrawIndexedIndirectCommand

	inline constexpr uint32_t MAX_DRAWS_OPAQUE        = 32768u;
	inline constexpr uint32_t MAX_DRAWS_OPAQUE_MASKED = 12288u;
	inline constexpr uint32_t MAX_DRAWS_TRANSPARENT   = 3072u;
	inline constexpr uint32_t MAX_DRAWS_FLASHLIGHT    = 1024u;
	inline constexpr uint32_t MAX_DRAWS_CSM0          = 8192u;
	inline constexpr uint32_t MAX_DRAWS_CSM1          = 2048u;
	inline constexpr uint32_t MAX_DRAWS_CSM2          = 2048u;
	inline constexpr uint32_t MAX_DRAWS_CSM3          = 2048u;

	// Struct-unit offsets (for GPU indexing: indirectDraws[DRAW_OFFSET_* + drawIdx])
	inline constexpr uint32_t DRAW_OFFSET_OPAQUE        = 0u;
	inline constexpr uint32_t DRAW_OFFSET_OPAQUE_MASKED = DRAW_OFFSET_OPAQUE        + MAX_DRAWS_OPAQUE;
	inline constexpr uint32_t DRAW_OFFSET_TRANSPARENT   = DRAW_OFFSET_OPAQUE_MASKED + MAX_DRAWS_OPAQUE_MASKED;
	inline constexpr uint32_t DRAW_OFFSET_FLASHLIGHT    = DRAW_OFFSET_TRANSPARENT   + MAX_DRAWS_TRANSPARENT;
	inline constexpr uint32_t DRAW_OFFSET_CSM0          = DRAW_OFFSET_FLASHLIGHT    + MAX_DRAWS_FLASHLIGHT;
	inline constexpr uint32_t DRAW_OFFSET_CSM1          = DRAW_OFFSET_CSM0          + MAX_DRAWS_CSM0;
	inline constexpr uint32_t DRAW_OFFSET_CSM2          = DRAW_OFFSET_CSM1          + MAX_DRAWS_CSM1;
	inline constexpr uint32_t DRAW_OFFSET_CSM3          = DRAW_OFFSET_CSM2          + MAX_DRAWS_CSM2;
	inline constexpr uint32_t DRAW_OFFSET_TOTAL         = DRAW_OFFSET_CSM3          + MAX_DRAWS_CSM3;

	inline constexpr uint32_t DRAW_BYTE_OFFSET_BY_SLOT[VIS_SLOT_COUNT] = {
		DRAW_OFFSET_OPAQUE        * INDIRECT_CMD_SIZE,
		DRAW_OFFSET_OPAQUE_MASKED * INDIRECT_CMD_SIZE,
		DRAW_OFFSET_TRANSPARENT   * INDIRECT_CMD_SIZE,
		DRAW_OFFSET_FLASHLIGHT    * INDIRECT_CMD_SIZE,
		DRAW_OFFSET_CSM0          * INDIRECT_CMD_SIZE,
		DRAW_OFFSET_CSM1          * INDIRECT_CMD_SIZE,
		DRAW_OFFSET_CSM2          * INDIRECT_CMD_SIZE,
		DRAW_OFFSET_CSM3          * INDIRECT_CMD_SIZE,
	};

	inline constexpr uint32_t MAX_DRAWS_BY_SLOT[VIS_SLOT_COUNT] = {
		MAX_DRAWS_OPAQUE, MAX_DRAWS_OPAQUE_MASKED, MAX_DRAWS_TRANSPARENT,
		MAX_DRAWS_FLASHLIGHT, MAX_DRAWS_CSM0, MAX_DRAWS_CSM1, MAX_DRAWS_CSM2, MAX_DRAWS_CSM3,
	};

	// --------------------
	// Task Dispatch Slots
	// --------------------
	inline constexpr uint32_t TASK_GROUP_SIZE = 32u;

	inline constexpr uint32_t MAX_TASK_DISPATCHES_OPAQUE        = MAX_DRAWS_OPAQUE;
	inline constexpr uint32_t MAX_TASK_DISPATCHES_OPAQUE_MASKED = MAX_DRAWS_OPAQUE_MASKED;
	inline constexpr uint32_t MAX_TASK_DISPATCHES_TRANSPARENT   = 0u;
	inline constexpr uint32_t MAX_TASK_DISPATCHES_FLASHLIGHT    = MAX_DRAWS_FLASHLIGHT;
	inline constexpr uint32_t MAX_TASK_DISPATCHES_CSM           = MAX_DRAWS_CSM0;

	inline constexpr uint32_t TASK_OFFSET_OPAQUE        = 0u;
	inline constexpr uint32_t TASK_OFFSET_OPAQUE_MASKED = TASK_OFFSET_OPAQUE        + MAX_TASK_DISPATCHES_OPAQUE;
	inline constexpr uint32_t TASK_OFFSET_TRANSPARENT   = TASK_OFFSET_OPAQUE_MASKED + MAX_TASK_DISPATCHES_OPAQUE_MASKED;
	inline constexpr uint32_t TASK_OFFSET_FLASHLIGHT    = TASK_OFFSET_TRANSPARENT   + MAX_TASK_DISPATCHES_TRANSPARENT;
	inline constexpr uint32_t TASK_OFFSET_CSM0          = TASK_OFFSET_FLASHLIGHT    + MAX_TASK_DISPATCHES_FLASHLIGHT;
	inline constexpr uint32_t TASK_OFFSET_CSM1          = TASK_OFFSET_CSM0          + MAX_TASK_DISPATCHES_CSM;
	inline constexpr uint32_t TASK_OFFSET_CSM2          = TASK_OFFSET_CSM1          + MAX_TASK_DISPATCHES_CSM;
	inline constexpr uint32_t TASK_OFFSET_CSM3          = TASK_OFFSET_CSM2          + MAX_TASK_DISPATCHES_CSM;
	inline constexpr uint32_t TASK_OFFSET_TOTAL         = TASK_OFFSET_CSM3          + MAX_TASK_DISPATCHES_CSM;

	inline constexpr uint32_t TASK_BYTE_OFFSET_BY_SLOT[VIS_SLOT_COUNT] = {
		TASK_OFFSET_OPAQUE        * TASK_GROUP_SIZE,
		TASK_OFFSET_OPAQUE_MASKED * TASK_GROUP_SIZE,
		TASK_OFFSET_TRANSPARENT   * TASK_GROUP_SIZE,
		TASK_OFFSET_FLASHLIGHT    * TASK_GROUP_SIZE,
		TASK_OFFSET_CSM0          * TASK_GROUP_SIZE,
		TASK_OFFSET_CSM1          * TASK_GROUP_SIZE,
		TASK_OFFSET_CSM2          * TASK_GROUP_SIZE,
		TASK_OFFSET_CSM3          * TASK_GROUP_SIZE
	};

	inline constexpr uint32_t MAX_TASK_DISPATCHES_BY_SLOT[VIS_SLOT_COUNT] = {
		MAX_TASK_DISPATCHES_OPAQUE,
		MAX_TASK_DISPATCHES_OPAQUE_MASKED,
		MAX_TASK_DISPATCHES_TRANSPARENT,
		MAX_TASK_DISPATCHES_FLASHLIGHT,
		MAX_TASK_DISPATCHES_CSM,
		MAX_TASK_DISPATCHES_CSM,
		MAX_TASK_DISPATCHES_CSM,
		MAX_TASK_DISPATCHES_CSM,
	};

	// -------------------
	// Renderer constants
	// -------------------
	inline constexpr uint32_t MAX_FRAME_INSTANCES_TOTAL     = 1000000;
	inline constexpr uint32_t MAX_FRAME_DRAW_COMMANDS_TOTAL = DRAW_OFFSET_TOTAL;
	inline constexpr uint32_t MAX_FRAMES_IN_FLIGHT          = 3u;
	//inline constexpr uint32_t MAX_LIGHTS                    = 16384u;
	inline constexpr uint32_t MAX_LIGHTS                    = 2048u;
	//inline constexpr uint32_t MAX_VISIBLE_LIGHTS            = 2048u;
	inline constexpr uint32_t MAX_PUSH_CONSTANT_SIZE        = 128u;
	inline constexpr uint32_t MAX_INSTANCES_PER_STREAM      = 262144u;
	inline constexpr uint32_t MAX_DRAW_BINS                 = 16384u;
	inline constexpr uint32_t BIN_TABLE_SIZE                = MAX_DRAW_BINS * 2;
	inline constexpr uint32_t INVALID_U32                   = 0xFFFFFFFFu;
	inline constexpr uint32_t MAX_GRAPHICS_PRIMARIES        = 3u;
	inline constexpr uint32_t MAX_MESHLET_VISIBILITY_BITS   = 33554432u;
	
	inline constexpr uint32_t TRANSFORM_DYNAMIC_BIT = 1u << 31;
	inline constexpr uint32_t TRANSFORM_INDEX_MASK  = ~TRANSFORM_DYNAMIC_BIT;
	inline constexpr uint32_t MAX_STATIC_TRANSFORMS = MAX_FRAME_INSTANCES_TOTAL;
	inline constexpr uint32_t MAX_DYNAMIC_TRANSFORMS = 32768u;

	inline constexpr uint32_t DEBUG_VERTS_PER_OBB     = 24u;
	inline constexpr uint32_t DEBUG_VERTS_PER_SPHERE  = 72u;     // 3 rings x 12 segments x 2
	inline constexpr uint32_t DEBUG_MAX_ITEMS         = 65536u;  // max debug items per frame
	inline constexpr uint32_t DEBUG_MAX_VERTS         = DEBUG_MAX_ITEMS * DEBUG_VERTS_PER_OBB;

	// Static lights in global list
	inline constexpr uint32_t LIGHT_LIST_STATIC_COUNT     = 1u;
	//inline constexpr uint32_t LIGHT_LIST_SLOT_DIRECTIONAL = 0u;
	inline constexpr uint32_t LIGHT_LIST_SLOT_FLASHLIGHT  = 0u;

	inline constexpr uint32_t LIGHT_FLAG_CASTS_SPOT_SHADOW  = 1u << 0;
	inline constexpr uint32_t LIGHT_FLAG_FLASHLIGHT         = 1u << 1;
	inline constexpr uint32_t LIGHT_FLAG_FLASHLIGHT_OFF     = 1u << 2;
	inline constexpr uint32_t CLUSTERS_TILE_SLICE_X         = 64u;
	inline constexpr uint32_t CLUSTERS_TILE_SLICE_Y         = 64u;
	inline constexpr uint32_t CLUSTERS_TILE_SLICE_Z         = 24u;
	//inline constexpr size_t MAX_VISIBLE_LIGHT_ID_GPU_BYTES = MAX_LIGHTS * sizeof(uint32_t);
	inline constexpr uint32_t MAX_LIGHTS_PER_CLUSTER        = 256u;
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
	inline constexpr uint32_t HI_Z_MIP_COUNT          = 11u;
	inline constexpr uint32_t HI_Z_MIN_MIP_COUNT      = 5u;

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
		ShadowBounds,
		InstanceCull,
		DrawBuild,
		Prepass,
		HiZGeneration,
		PrepassLate,
		HiZGenerationLate,
		VelocityResolve,
		DirectionalCSMAtlas,
		FlashlightShadow,
		MaterialResolve,
		ClusteredLights,
		SSAO,
		ScreenSpaceContactShadows,
		Skybox,
		OpaqueForward,
		OpaqueLighting,
		TransparentForward,
		TransparentResolve,
		DebugDrawBuild,
		DebugLineDraw,
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
		Forward_v,
		Transparent_f,
		TransparentResolve_c,
		Skybox_v,
		Skybox_f,

		Wireframe_m,
		Wireframe_v,
		Wireframe_f,

		VelocityResolve_c,
		MaterialResolve_c,
		OpaqueLighting_c,

		ShadowBounds_c,
		InstanceCull_c,
		DrawArgs_c,
		DrawScatter_c,
		DrawEmit_c,
		DrawPlace_c,

		ExposureReduce_c,
		ExposureFinalize_c,
		FinalComposite_c,

		DebugCount_c,
		DebugArgs_c,
		DebugBuild_c,
		LineDebug_v,
		LineDebug_f,

		HDRToCubemap_c,
		SpecularPrefilter_c,
		DiffuseIrradiance_c,
		BRDFLUT_c,

		MeshletCull_t,

		Prepass_m,
		PrepassMasked_m,
		Prepass_v,
		Prepass_f,
		PrepassMasked_v,
		PrepassMasked_f,

		Shadow_t,
		Shadow_m,
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

		GBufferDebug_c,

		Count
	};

	inline constexpr size_t SHADER_COUNT = static_cast<size_t>(Renderer_Shader::Count);

	enum class Renderer_Pipeline
	{
		TransparentForward,
		TransparentResolve,
		Skybox,

		VelocityResolve,
		WireframeMesh,
		Wireframe,

		PrepassMesh,
		PrepassMaskedMesh,
		ShadowMesh,

		ShadowBounds,
		InstanceCull,
		DrawArgs,
		DrawScatter,
		DrawEmit,
		DrawPlace,

		MaterialResolve,
		OpaqueLighting,

		ExposureReduce,
		ExposureFinalize,
		FinalComposite,

		DebugCount,
		DebugArgs,
		DebugBuild,
		LineDebug,

		HDRToCubemap,
		SpecularPrefilter,
		DiffuseIrradiance,
		BRDFLUT,

		Prepass,
		PrepassMasked,
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

		LightCull,

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

		GBufferDebug,

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
		Visibility,
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
		MaterialAlbedoRough,
		MaterialNormal,
		MaterialMetal,
		MaterialEmissive,

		// Used as chromatic aberration output
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
		// Global — persistent
		InstanceInputs,
		DrawBinKeys,
		Mesh,
		Material,
		Vertex,
		Index,
		Meshlet,
		MeshletVertices,
		MeshletTriangles,
		StaticTransforms,
		Luminance,

		// Frame — transient
		DynamicTransforms,
		MotionMatrices,
		Lights,
		InstanceVisibility,
		MeshletVisibilityA,
		MeshletVisibilityB,
		VisibleCount,
		VisibleInstances,
		InstanceCursors,
		InstanceStreams,
		DrawInstanceIDs,
		IndirectDraws,
		IndirectDrawCounts,
		DrawBins,
		DrawBinCounters,
		ShadowCullData,
		DrawStats,
		DispatchIndirectArgs,
		TaskDispatch,
		DebugCounts,
		DebugItems,
		DebugVertex,
		DebugDraw,
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

		Count
	};

	inline constexpr size_t ADDRESS_TABLE_BUFFER_COUNT = static_cast<size_t>(Renderer_Buffer::Count);

	inline constexpr uint32_t DEBUG_MASK_OBB     = 1u << 0;
	inline constexpr uint32_t DEBUG_MASK_SPHERE  = 1u << 1; // unused
	inline constexpr uint32_t DEBUG_MASK_AABB    = 1u << 2; // unused

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
		AA_TAA      // Temporal Anti-Aliasing, deferred only
	};

	enum class RenderingMode
	{
		VISIBILITY_DEFERRED,

		MESH_SHADERS, // Implies visibility deferred
		UNDEFINED
	};

	//enum class ToneMapper
	//{
	//	TM_ACESFILM,
	//	TM_GT7
	//};

	// Define this somewhere
	struct alignas(4) RenderToggles
	{
		uint32_t enableLensFlare           = 0;
		uint32_t enableChromaticAberration = 0;
		uint32_t enableSSS                 = 0;
		uint32_t enableFlashlight          = 0;

		uint32_t aaMode                    = 0;
		uint32_t aoMode                    = 0;
		uint32_t enableShadows             = 0;
		uint32_t enableVolumetrics         = 0;

		uint32_t activeEnvMap              = 0;
		uint32_t disableOcclusionCull      = 0;
		uint32_t renderingMode             = 0;
		uint32_t debugView                 = 0;

		float depthScale                   = 0.0f;
		uint32_t enableWireframe           = 0;
		uint32_t pad0;
		uint32_t pad1;

		uint32_t enableProfilerView        = 0;
		uint32_t enableSettings            = 0;
		uint32_t enableBloom               = 0;
		float bloomIntensity               = 0.0;

		uint32_t showOpaqueOBBs            = 0;
		uint32_t showTransparentOBBs       = 0;
		uint32_t activeInstanceCount       = 0;
		uint32_t activeLightCount          = 0;
	};

	enum class ImageAccess
	{
		Undefined,

		TransferSrc,
		TransferDst,

		ComputeRead,
		ComputeWrite,

		Read,
		Write,

		MeshShaderRead,

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

	enum class DebugState : uint32_t
	{
		Off,
		Wireframe,
		OBBLine,
		ShadedOverlay,
	};

	struct RenderStateInfo
	{
	public:
		void ResetDebugMask() { m_debugState = DebugState::Off; }
		void SetDebugMask(DebugState mask) { m_debugState = mask; }

		bool IsWireframeOn() const noexcept { return m_debugState == DebugState::Wireframe; }
		bool IsObbLineOn() const noexcept { return m_debugState == DebugState::OBBLine; }
		bool IsShadedOverlayOn() const noexcept { return m_debugState == DebugState::ShadedOverlay; }

		bool InstancesActive() const noexcept { return m_renderToggles.activeInstanceCount > 0; }
		bool LightsActive() const noexcept { return m_renderToggles.activeLightCount > 0; }

		bool IsMeshShaderPath() const noexcept
		{
			return static_cast<RenderingMode>(m_renderToggles.renderingMode) == RenderingMode::MESH_SHADERS;
		}
		uint32_t RenderMode() const noexcept { return m_renderToggles.renderingMode; }

		// Cuts down minimum passes
		bool DebugRenderFastPath() const noexcept { return IsWireframeOn() || IsShadedOverlayOn(); }

		bool DebugRendering() const noexcept { return DebugRenderFastPath() || IsObbLineOn(); }

		void UpdateToggles(RenderToggles toggles) { m_renderToggles = toggles; }

		bool DrawImgui() const noexcept { return m_renderToggles.enableProfilerView || m_renderToggles.enableSettings; }
		bool FlashlightOn() const noexcept { return m_renderToggles.enableFlashlight; }
		bool CopyPostAAImage() const noexcept
		{
			return m_renderToggles.aaMode != static_cast<uint32_t>(AntiAliasingMethod::AA_OFF) &&
				m_renderToggles.aaMode != static_cast<uint32_t>(AntiAliasingMethod::AA_TAA);
		}

		void UpdateTemporal(bool temporalValid, bool hiZValid)
		{
			m_bTemporalValid = temporalValid;
			m_bHiZValid = hiZValid;
		}

		bool IsShadowsOn() const noexcept { return m_renderToggles.enableShadows; }
		bool IsFlashlightOn() const noexcept { return m_renderToggles.enableFlashlight; }
		bool IsScreenSpaceShadowsOn() const noexcept { return m_renderToggles.enableSSS; }
		bool IsVolumetricsOn() const noexcept { return m_renderToggles.enableVolumetrics; }

		bool IsTemporalValid() const noexcept { return m_bTemporalValid; }
		bool IsHiZValid() const noexcept { return m_bHiZValid; }

		bool IsTaaOn() const noexcept
		{
			return m_renderToggles.aaMode == static_cast<uint32_t>(AntiAliasingMethod::AA_TAA);
		}

		void ResetRenderToggles() { m_renderToggles = {}; }

		uint32_t GetLightCount() const noexcept { return m_renderToggles.activeLightCount; }
		uint32_t GetInstanceCount() const noexcept { return m_renderToggles.activeInstanceCount; }

		bool m_bStateChanged         = false;
	private:

		bool m_bTemporalValid        = false;
		bool m_bHiZValid             = false;

		DebugState m_debugState = DebugState::Off;

		RenderToggles m_renderToggles;
	};


	enum class DebugView : uint32_t
	{
		Off = 0,
		Albedo,
		Normals,
		Roughness,
		Metallic,
		Emissive,
		SSAO,
		SSShadows,
		Cascades,
		VisInstance,
		VisTriangle,
		VisLod,
		Meshlets,
		//MeshletFacing,
		Count
	};

	inline constexpr uint32_t DebugViewBit(DebugView v)
	{
		return 1u << static_cast<uint32_t>(v);
	}

	inline constexpr uint32_t DBG_CAPS_DEFERRED =
		DebugViewBit(DebugView::Off)         | DebugViewBit(DebugView::Albedo)      |
		DebugViewBit(DebugView::Normals)     | DebugViewBit(DebugView::Roughness)   |
		DebugViewBit(DebugView::Metallic)    | DebugViewBit(DebugView::Emissive)    |
		DebugViewBit(DebugView::SSAO)        | DebugViewBit(DebugView::SSShadows)   |
		DebugViewBit(DebugView::Cascades)    | DebugViewBit(DebugView::VisInstance) |
		DebugViewBit(DebugView::VisTriangle) | DebugViewBit(DebugView::VisLod);

	inline constexpr uint32_t DBG_CAPS_MESH =
		DBG_CAPS_DEFERRED                 |
		DebugViewBit(DebugView::Meshlets);

	inline constexpr uint32_t DebugCapsForMode(RenderingMode mode)
	{
		switch (mode)
		{
			case RenderingMode::MESH_SHADERS:        return DBG_CAPS_MESH;
			case RenderingMode::VISIBILITY_DEFERRED: return DBG_CAPS_DEFERRED;
		}
	}

	inline constexpr bool DebugViewSupported(uint32_t caps, uint32_t view)
	{
		return view < static_cast<uint32_t>(DebugView::Count)
			&& (caps & (1u << view)) != 0u;
	}

	struct DebugViewEntry
	{
		DebugView   view;
		const char* label;
	};

	inline constexpr DebugViewEntry DEBUG_VIEW_TABLE[] = {
		{ DebugView::Off,           "Complete"        },
		{ DebugView::Albedo,        "Albedo"          },
		{ DebugView::Normals,       "Normals"         },
		{ DebugView::Roughness,     "Roughness"       },
		{ DebugView::Metallic,      "Metallic"        },
		{ DebugView::Emissive,      "Emissive"        },
		{ DebugView::SSAO,          "SSAO"            },
		{ DebugView::SSShadows,     "Contact Shadows" },
		{ DebugView::Cascades,      "Cascade Splits"  },
		{ DebugView::VisInstance,   "Vis: Instance"   },
		{ DebugView::VisTriangle,   "Vis: Triangle"   },
		{ DebugView::VisLod,        "Vis: LOD"        },
		{ DebugView::Meshlets,      "Meshlets"        },
		//{ DebugView::MeshletFacing, "MeshletFacing"   }
	};
}
