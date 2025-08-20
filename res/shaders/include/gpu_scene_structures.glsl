#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_ARB_gpu_shader_int64 : require

#ifndef GPU_SCENE_STRUCTURES_GLSL
#define GPU_SCENE_STRUCTURES_GLSL

struct SceneData {
    mat4 view;
    mat4 proj;
    mat4 viewproj;
    vec4 ambientColor;
    vec4 sunlightDirection; // .w = power
    vec4 sunlightColor;
    vec4 cameraPos;
};

// Number of env sets stored in the buffer (must match C++ side)
const uint MAX_ENV_SETS = 16u;

struct EnvMapIndexArray {
    uvec4 indices[MAX_ENV_SETS];
    // x = diffuseMapIndex
    // y = specularMapIndex
    // z = brdfLUTIndex
    // w = skyboxMapIndex
};

struct AABB {
	vec3 vmin; // origin: 0.5f * (vmin + vmax)
	vec3 vmax; // extent: 0.5f * (vmax - vmin)
	vec3 origin;
	vec3 extent;
	float sphereRadius;
};

struct Vertex {
    vec3 position;
    vec3 normal;
    vec2 uv;
    vec4 color;
};

const uint PASS_OPAQUE      = 0u;
const uint PASS_TRANSPARENT = 1u;

struct Material {
    vec4 colorFactor;
    vec2 metalRoughFactors;

    vec3 emissiveColor;
    float emissiveStrength;

    float ambientOcclusion;
    float normalScale;
    float alphaCutoff;
    uint passType;

    uint albedoID;
    uint metalRoughnessID;
    uint normalID;
    uint aoID;
    uint emissiveID;
};

struct Mesh {
    AABB localAABB;
    uint firstIndex;
    uint indexCount;
    uint vertexOffset;
    uint vertexCount;
};

// All defined as VkDrawIndexedIndirectCommand
const uint DRAW_STATIC        = 0u;
const uint DRAW_MULTI_STATIC  = 1u;
const uint DRAW_DYNAMIC       = 2u;
const uint DRAW_MULTI_DYNAMIC = 3u;

struct Instance {
    uint meshID;
    uint materialID;
    uint transformID;
    uint drawType;
    uint passType;
};

// Enum address buffer types
const uint ABT_VisibleInstances  = 0u; // frame
const uint ABT_IndirectDraws     = 1u; // frame
const uint ABT_Transforms        = 2u; // global
const uint ABT_Material          = 3u; // global
const uint ABT_Mesh              = 4u; // global
const uint ABT_Vertex            = 5u; // global
const uint ABT_Index             = 6u; // global
const uint ABT_Count             = 7u;

struct GPUAddressTable {
    uint64_t addrs[ABT_Count];
};

struct IndirectDrawCmd {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

// GPU-only buffers

// instances
layout(buffer_reference, scalar) readonly buffer VisibleInstances {
    Instance instances[];
};
// indirect draws
layout(buffer_reference, scalar) readonly buffer IndirectDraws {
    IndirectDrawCmd indirectDraws[];
};

// materials, vertices, indices, all ready at render time and uploaded at end of asset loading
layout(buffer_reference, scalar) readonly buffer MaterialBuffer {
    Material materials[];
};

layout(buffer_reference, scalar) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(buffer_reference, scalar) readonly buffer IndexBuffer {
    uint indices[];
};

layout(buffer_reference, scalar) readonly buffer TransformsBuffer {
    mat4 transforms[];
};

layout(buffer_reference, scalar) readonly buffer MeshBuffer {
    Mesh meshes[];
};

#endif