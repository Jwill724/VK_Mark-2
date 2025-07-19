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

struct EnvMapBindingSet {
    vec4 mapIndices[MAX_ENV_SETS];
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


struct Material {
    vec4 colorFactor;
    vec2 metalRoughFactors;

    uint albedoLUTIndex;
    uint metalRoughLUTIndex;
    uint normalLUTIndex;
    uint aoLUTIndex;

    vec3 emissiveColor;
    float emissiveStrength;

    float ambientOcclusion;
    float normalScale;
    float alphaCutoff;
    uint passType;
};

struct Mesh {
    AABB localAABB;
    AABB worldAABB;
    uint drawRangeID;
};


struct Instance {
    uint instanceID;
    uint materialID;
    uint meshID;
    uint transformID;
};

// Enum address buffer types
const uint ABT_OpaqueInstances          = 0u;
const uint ABT_OpaqueIndirectDraws      = 1u;
const uint ABT_TransparentInstances     = 2u;
const uint ABT_TransparentIndirectDraws = 3u;
const uint ABT_Material                 = 4u;
const uint ABT_Mesh                     = 5u;
const uint ABT_DrawRange                = 6u;
const uint ABT_Vertex                   = 7u;
const uint ABT_Index                    = 8u;
const uint ABT_Transforms               = 9u;
const uint ABT_VisibleCount             = 10u;
const uint ABT_VisibleMeshIDs           = 11u;
const uint ABT_Count                    = 12u;

struct GPUAddressTable {
    uint64_t addrs[ABT_Count];
};

struct GPUDrawRange {
    uint firstIndex;
    uint indexCount;
    uint vertexOffset;
    uint vertexCount;
};

struct IndirectDrawCmd {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

// GPU-only buffers

// Opaque and transparent data is a render time upload
// Only visible data makes it through

// Opaque
layout(buffer_reference, scalar) readonly buffer OpaqueInstances {
    Instance opaqueInstances[];
};
layout(buffer_reference, scalar) readonly buffer OpaqueIndirectDraws {
    IndirectDrawCmd opaqueIndirect[];
};

// Transparent
layout(buffer_reference, scalar) readonly buffer TransparentInstances {
    Instance transparentInstances[];
};
layout(buffer_reference, scalar) readonly buffer TransparentIndirectDraws {
    IndirectDrawCmd transparentIndirect[];
};

// ranges, materials, vertices, indices, all ready at render time and uploaded at end of asset loading
layout(buffer_reference, scalar) readonly buffer DrawRangeBuffer {
    GPUDrawRange ranges[];
};

layout(buffer_reference, scalar) readonly buffer MaterialBuffer {
    Material materials[];
};

layout(buffer_reference, scalar) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(buffer_reference, scalar) readonly buffer IndexBuffer {
    uint indices[];
};

layout(buffer_reference, scalar) readonly buffer TransformsListBuffer {
    mat4 transforms[];
};

// In current cpu based setup, worldAABBs are done on cpu after main upload,
// all thats present here currently is localAABB and drawRangeIndex
layout(buffer_reference, scalar) readonly buffer MeshBuffer {
    Mesh meshes[];
};


// Inactives
layout(buffer_reference, scalar) writeonly buffer VisibleCountBuffer {
    uint visibleCount;
};

layout(buffer_reference, scalar) writeonly buffer VisibleMeshIDBuffer {
    uint visibleMeshIDs[];
};

#endif