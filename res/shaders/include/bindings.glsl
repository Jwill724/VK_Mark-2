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
const uint ABT_Luminance              = 6u;

// --- Frame (written/reset each frame) ---

const uint ABT_Transforms             = 7u;
const uint ABT_PrevTransforms         = 8u;
const uint ABT_Lights                 = 9u;

// Culling outputs: visibility flags, one uint per instance
// bit 0 = primary visible, bits 1..4 = csm cascade 0..3, bit 5 = flashlight
const uint ABT_InstanceVisibility     = 10u;

// Atomic counters
// Read back after culling stage to build DispatchIndirectArgs for draw build
const uint ABT_VisibleCount           = 11u;

const uint ABT_VisibleInstances       = 12u;

const uint ABT_InstanceCursors        = 13u;

const uint ABT_InstanceStreams        = 14u;

const uint ABT_DrawInstanceIDs        = 15u;

const uint ABT_IndirectDraws          = 16u;

const uint ABT_IndirectDrawCounts     = 17u;

// DrawBin array: one bin per {meshID, materialID} pair, per stream
// Flat layout: bins[streamIdx * MAX_BINS + binIdx]
const uint ABT_DrawBins               = 18u;

// Atomic counters for DrawBin instance counts (prefix-summed in draw build)
const uint ABT_DrawBinCounters        = 19u;

const uint ABT_ShadowCullData         = 20u;

const uint ABT_DrawStats              = 21u;

const uint ABT_DispatchIndirectArgs   = 22u;

const uint ABT_DebugCounts            = 23u;
const uint ABT_DebugItems             = 24u;
const uint ABT_DebugVertex            = 25u;
const uint ABT_DebugDraw              = 26u;

// Light culling
const uint ABT_VisibleLightCount      = 27u;
const uint ABT_VisibleLightIDs        = 28u;

// Clustered shading
const uint ABT_ClusterCounts          = 29u;
const uint ABT_ClusterOffsets         = 30u;
const uint ABT_ClusterCursors         = 31u;
const uint ABT_ClusterLightIDs        = 32u;
const uint ABT_ClusterTileSliceRanges = 33u;
const uint ABT_ClusterScanScratch     = 34u;

// CMAA2
const uint ABT_Cmaa2Control           = 35u;
const uint ABT_Cmaa2ShapeCandidates   = 36u;
const uint ABT_Cmaa2DeferredLocations = 37u;
const uint ABT_Cmaa2DeferredItems     = 38u;
const uint ABT_Cmaa2DeferredHeads     = 39u;

const uint ABT_Count                  = 40u;


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
const uint INDIRECT_DISPATCH_SLOT_STREAM_OPAQUE      = 0u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_TRANSPARENT = 1u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_FLASHLIGHT  = 2u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_CSM0        = 3u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_CSM1        = 4u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_CSM2        = 5u;
const uint INDIRECT_DISPATCH_SLOT_STREAM_CSM3        = 6u;
const uint INDIRECT_DISPATCH_SLOT_SCATTER            = 7u;
const uint INDIRECT_DISPATCH_SLOT_DEBUG_BUILD        = 8u;
const uint INDIRECT_DISPATCH_SLOT_LIGHTS             = 9u;
const uint INDIRECT_DISPATCH_SLOT_CLUSTERS           = 10u;
const uint INDIRECT_DISPATCH_SLOT_CMAA2_SHAPES       = 11u;
const uint INDIRECT_DISPATCH_SLOT_CMAA2_DEFERRED     = 12u;
const uint INDIRECT_DISPATCH_SLOT_COUNT              = 13u;

// Visibility/Draw slots
const uint VIS_SLOT_OPAQUE        = 0u;
const uint VIS_SLOT_TRANSPARENT   = 1u;
const uint VIS_SLOT_FLASHLIGHT    = 2u;
const uint VIS_SLOT_CSM0          = 3u;
const uint VIS_SLOT_CSM1          = 4u;
const uint VIS_SLOT_CSM2          = 5u;
const uint VIS_SLOT_CSM3          = 6u;

const uint VIS_SLOT_COUNT         = 7u;


#endif
