#ifndef SET_BINDINGS_GLSL
#define SET_BINDINGS_GLSL

const uint GLOBAL_SET = 0u;
const uint FRAME_SET = 1u;
const uint PUSH_SET = 2u;

// both global and frame owned
const uint ADDRESS_TABLE_BINDING = 0u;

// global set specific
const uint GLOBAL_BINDING_ENV_INDEX = 1u;
const uint GLOBAL_BINDING_SSAO_KERNEL = 2u;
const uint GLOBAL_BINDING_DEBUG_INLINE = 3u;
const uint GLOBAL_BINDING_SAMPLER_CUBE = 4u;
const uint GLOBAL_BINDING_STORAGE_IMAGE = 5u;
const uint GLOBAL_BINDING_COMBINED_SAMPLER = 6u;

// Frame set specific
const uint FRAME_BINDING_SCENE = 1u;
const uint FRAME_BINDING_CSM = 2u;

// Push bindings
const uint PUSH_BINDING_DEPTH_TEX = 0u;
const uint PUSH_BINDING_NORMAL_TEX = 1u;
const uint PUSH_BINDING_OUTPUT_TEX = 2u;
const uint PUSH_BINDING_INPUT_1_TEX = 3u;
const uint PUSH_BINDING_INPUT_2_TEX = 4u;
const uint PUSH_BINDING_NOISE_TEX = 5u;

#endif