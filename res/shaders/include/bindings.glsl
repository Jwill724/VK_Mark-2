#ifndef SET_BINDINGS_GLSL
#define SET_BINDINGS_GLSL

// ================================
// === ADDRESS BUFFER TABLE IDs ===
// ================================

// --- Global (persistent across frames) ---
const uint ABT_InstanceInputs         = 0u;
const uint ABT_DrawBinKeys            = 1u;
const uint ABT_Mesh                   = 2u;
const uint ABT_Material               = 3u;
const uint ABT_Vertex                 = 4u;
const uint ABT_Index                  = 5u;
const uint ABT_Meshlet                = 6u;
const uint ABT_MeshletVertices        = 7u;
const uint ABT_MeshletTriangles       = 8u;
const uint ABT_StaticTransforms       = 9u;
const uint ABT_Luminance              = 10u;
const uint ABT_SHIrradiance           = 11u;
const uint ABT_BLASAddresses          = 12u;
const uint ABT_RTRows                 = 13u;

// --- Frame (written/reset each frame) ---
const uint ABT_DynamicTransforms      = 14u;
const uint ABT_MotionMatrices         = 15u;
const uint ABT_Lights                 = 16u;

const uint ABT_RTInstances            = 17u;
const uint ABT_RTRayList              = 18u;

const uint ABT_InstanceVisibility     = 19u;
const uint ABT_MeshletVisibilityA     = 20u;
const uint ABT_MeshletVisibilityB     = 21u;

const uint ABT_VisibleCount           = 22u;

const uint ABT_VisibleInstances       = 23u;

const uint ABT_InstanceCursors        = 24u;

const uint ABT_InstanceStreams        = 25u;

const uint ABT_DrawInstanceIDs        = 26u;

const uint ABT_IndirectDrawCounts     = 27u;

const uint ABT_DrawBins               = 28u;

const uint ABT_DrawBinCounters        = 29u;

const uint ABT_ShadowCullData         = 30u;

const uint ABT_DrawStats              = 31u;

const uint ABT_DispatchIndirectArgs   = 32u;

const uint ABT_TaskDispatch           = 33u;

const uint ABT_DebugCounts            = 34u;
const uint ABT_DebugItems             = 35u;
const uint ABT_DebugVertex            = 36u;
const uint ABT_DebugDraw              = 37u;

// Light culling
const uint ABT_VisibleLightCount      = 38u;
const uint ABT_VisibleLightIDs        = 39u;

// Clustered shading
const uint ABT_ClusterCounts              = 40u;
const uint ABT_ClusterOffsets             = 41u;
const uint ABT_ClusterCursors             = 42u;
const uint ABT_ClusterLightIDs            = 43u;
const uint ABT_ClusterTileSliceRanges     = 44u;
const uint ABT_ClusterScanScratch         = 45u;
const uint ABT_ClusterTileTransparentNear = 46u;

const uint ABT_ShadowInvalidVolumes       = 47u;

const uint ABT_Count                      = 48u;

const uint MAX_SHADOW_INVALID_VOLUMES = 64u;

// =============================
// === SET_BINDINGS_BINDINGS ===
// =============================

const uint GLOBAL_SET = 0u;
const uint FRAME_SET  = 1u;
const uint PUSH_SET   = 2u;

// both global and frame owned
const uint ADDRESS_TABLE_BINDING            = 0u;

// global set specific
const uint GLOBAL_BINDING_DEBUG_INLINE      = 1u;
const uint GLOBAL_BINDING_SAMPLER_CUBE      = 2u;
const uint GLOBAL_BINDING_COMBINED_SAMPLER  = 3u;

// Frame set specific UBOs
const uint FRAME_BINDING_SCENE      = 1u;
const uint FRAME_BINDING_CSM        = 2u;
const uint FRAME_BINDING_CLUSTERED  = 3u;
const uint FRAME_BINDING_VOLUMETRIC = 4u;
const uint FRAME_BINDING_TLAS       = 5u;

// Push bindings for images
const uint PUSH_BINDING_READ_1   = 0u;
const uint PUSH_BINDING_READ_2   = 1u;
const uint PUSH_BINDING_READ_3   = 2u;
const uint PUSH_BINDING_READ_4   = 3u;
const uint PUSH_BINDING_READ_5   = 4u;
const uint PUSH_BINDING_READ_6   = 5u;
const uint PUSH_BINDING_READ_7   = 6u;
const uint PUSH_BINDING_READ_8   = 7u;
const uint PUSH_BINDING_READ_9   = 8u;
const uint PUSH_BINDING_READ_10  = 9u;
const uint PUSH_BINDING_READ_11  = 10u;
const uint PUSH_BINDING_WRITE_1  = 11u;
const uint PUSH_BINDING_WRITE_2  = 12u;
const uint PUSH_BINDING_WRITE_3  = 13u;
const uint PUSH_BINDING_WRITE_4  = 14u;
const uint PUSH_BINDING_WRITE_5  = 15u;

// Indirect dispatch args
const uint INDIRECT_DISPATCH_SLOT_STREAM_OPAQUE        = 0u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_OPAQUE_MASKED = 1u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_TRANSPARENT   = 2u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_FLASHLIGHT    = 3u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_CSM0          = 4u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_CSM1          = 5u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_CSM2          = 6u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_CSM3          = 7u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_VOLUMETRIC    = 8u;
const uint INDIRECT_DISPATCH_SLOT_SCATTER              = 9u;
const uint INDIRECT_DISPATCH_SLOT_DEBUG_BUILD          = 10u;
const uint INDIRECT_DISPATCH_SLOT_LIGHTS               = 11u;
const uint INDIRECT_DISPATCH_SLOT_CLUSTERS             = 12u;
const uint INDIRECT_DISPATCH_SLOT_REFLECT_RAYS         = 13u;
const uint INDIRECT_DISPATCH_SLOT_SHADOW_RAYS          = 14u;
const uint INDIRECT_DISPATCH_SLOT_TRANSPARENCY_RAYS    = 15u;
const uint INDIRECT_DISPATCH_SLOT_COUNT                = 16u;

// Visibility/Draw slots
const uint VIS_SLOT_OPAQUE        = 0u;
const uint VIS_SLOT_OPAQUE_MASKED = 1u;
const uint VIS_SLOT_TRANSPARENT   = 2u;
const uint VIS_SLOT_FLASHLIGHT    = 3u;
const uint VIS_SLOT_CSM0          = 4u;
const uint VIS_SLOT_CSM1          = 5u;
const uint VIS_SLOT_CSM2          = 6u;
const uint VIS_SLOT_CSM3          = 7u;
const uint VIS_SLOT_VOLUMETRIC    = 8u;

const uint VIS_SLOT_COUNT         = 9u;

// Ray tracing ray slots
const uint RT_RAY_SLOT_REFLECT      = 0u;
const uint RT_RAY_SLOT_SHADOW       = 1u;
const uint RT_RAY_SLOT_TRANSPARENCY = 2u;
const uint RT_RAY_SLOT_COUNT        = 3u;

#endif
