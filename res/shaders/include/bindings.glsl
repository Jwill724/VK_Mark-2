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

// --- Frame (written/reset each frame) ---
const uint ABT_DynamicTransforms      = 11u;
const uint ABT_MotionMatrices         = 12u;
const uint ABT_Lights                 = 13u;

const uint ABT_InstanceVisibility     = 14u;
const uint ABT_MeshletVisibilityA     = 15u;
const uint ABT_MeshletVisibilityB     = 16u;

const uint ABT_VisibleCount           = 17u;

const uint ABT_VisibleInstances       = 18u;

const uint ABT_InstanceCursors        = 19u;

const uint ABT_InstanceStreams        = 20u;

const uint ABT_DrawInstanceIDs        = 21u;

const uint ABT_IndirectDraws          = 22u;

const uint ABT_IndirectDrawCounts     = 23u;

const uint ABT_DrawBins               = 24u;

const uint ABT_DrawBinCounters        = 25u;

const uint ABT_ShadowCullData         = 26u;

const uint ABT_DrawStats              = 27u;

const uint ABT_DispatchIndirectArgs   = 28u;

const uint ABT_TaskDispatch           = 29u;

const uint ABT_DebugCounts            = 30u;
const uint ABT_DebugItems             = 31u;
const uint ABT_DebugVertex            = 32u;
const uint ABT_DebugDraw              = 33u;

// Light culling
const uint ABT_VisibleLightCount      = 34u;
const uint ABT_VisibleLightIDs        = 35u;

// Clustered shading
const uint ABT_ClusterCounts          = 36u;
const uint ABT_ClusterOffsets         = 37u;
const uint ABT_ClusterCursors         = 38u;
const uint ABT_ClusterLightIDs        = 39u;
const uint ABT_ClusterTileSliceRanges = 40u;
const uint ABT_ClusterScanScratch     = 41u;

// CMAA2
const uint ABT_Cmaa2Control           = 42u;
const uint ABT_Cmaa2ShapeCandidates   = 43u;
const uint ABT_Cmaa2DeferredLocations = 44u;
const uint ABT_Cmaa2DeferredItems     = 45u;
const uint ABT_Cmaa2DeferredHeads     = 46u;

const uint ABT_Count                  = 47u;


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
const uint PUSH_BINDING_WRITE_1  = 9u;
const uint PUSH_BINDING_WRITE_2  = 10u;
const uint PUSH_BINDING_WRITE_3  = 11u;
const uint PUSH_BINDING_WRITE_4  = 12u;
const uint PUSH_BINDING_WRITE_5  = 13u;

// Indirect dispatch args
const uint INDIRECT_DISPATCH_SLOT_STREAM_OPAQUE        = 0u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_OPAQUE_MASKED = 1u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_TRANSPARENT   = 2u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_FLASHLIGHT    = 3u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_CSM0          = 4u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_CSM1          = 5u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_CSM2          = 6u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_CSM3          = 7u;
const uint INDIRECT_DISPATCH_SLOT_SCATTER              = 8u;
const uint INDIRECT_DISPATCH_SLOT_DEBUG_BUILD          = 9u;
const uint INDIRECT_DISPATCH_SLOT_LIGHTS               = 10u;
const uint INDIRECT_DISPATCH_SLOT_CLUSTERS             = 11u;
const uint INDIRECT_DISPATCH_SLOT_CMAA2_SHAPES         = 12u;
const uint INDIRECT_DISPATCH_SLOT_CMAA2_DEFERRED       = 13u;
const uint INDIRECT_DISPATCH_SLOT_COUNT                = 14u;

// Visibility/Draw slots
const uint VIS_SLOT_OPAQUE        = 0u;
const uint VIS_SLOT_OPAQUE_MASKED = 1u;
const uint VIS_SLOT_TRANSPARENT   = 2u;
const uint VIS_SLOT_FLASHLIGHT    = 3u;
const uint VIS_SLOT_CSM0          = 4u;
const uint VIS_SLOT_CSM1          = 5u;
const uint VIS_SLOT_CSM2          = 6u;
const uint VIS_SLOT_CSM3          = 7u;

const uint VIS_SLOT_COUNT         = 8u;


#endif
