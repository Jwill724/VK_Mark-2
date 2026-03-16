#ifndef SET_BINDINGS_GLSL
#define SET_BINDINGS_GLSL

const uint GLOBAL_SET                = 0u;
const uint FRAME_SET                 = 1u;
const uint PUSH_SET                  = 2u;

// both global and frame owned
const uint ADDRESS_TABLE_BINDING     = 0u;

// global set specific
const uint GLOBAL_BINDING_ENV_INDEX        = 1u;
const uint GLOBAL_BINDING_DEBUG_INLINE     = 2u;
const uint GLOBAL_BINDING_SAMPLER_CUBE     = 3u;
const uint GLOBAL_BINDING_COMBINED_SAMPLER = 4u;

// Frame set specific UBOs
const uint FRAME_BINDING_SCENE       = 1u;
const uint FRAME_BINDING_CSM         = 2u;
const uint FRAME_BINDING_CLUSTERED   = 3u;

// Push bindings
const uint PUSH_BINDING_INPUT_1_TEX  = 0u;
const uint PUSH_BINDING_INPUT_2_TEX  = 1u;
const uint PUSH_BINDING_INPUT_3_TEX  = 2u;
const uint PUSH_BINDING_INPUT_4_TEX  = 3u;
const uint PUSH_BINDING_INPUT_5_TEX  = 4u;
const uint PUSH_BINDING_OUTPUT_1_TEX = 5u;
const uint PUSH_BINDING_OUTPUT_2_TEX = 6u;
const uint PUSH_BINDING_OUTPUT_3_TEX = 7u;
const uint PUSH_BINDING_OUTPUT_4_TEX = 8u;
const uint PUSH_BINDING_OUTPUT_5_TEX = 9u;

#endif
