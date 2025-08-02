#ifndef SET_BINDINGS_GLSL
#define SET_BINDINGS_GLSL

const uint GLOBAL_SET = 0;
const uint FRAME_SET = 1;

// both global and frame owned
const uint ADDRESS_TABLE_BINDING = 0;

// global set specific
const uint GLOBAL_BINDING_ENV_INDEX = 1;
const uint GLOBAL_BINDING_SAMPLER_CUBE = 2;
const uint GLOBAL_BINDING_STORAGE_IMAGE = 3;
const uint GLOBAL_BINDING_COMBINED_SAMPLER = 4;

// Frame set specific
const uint FRAME_BINDING_SCENE = 1;

#endif