#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_ARB_gpu_shader_int64 : enable

#ifndef INPUT_STRUCTURES_GLSL
#define INPUT_STRUCTURES_GLSL

struct AABB {
	vec3 vmin; // origin: 0.5f * (vmin + vmax)
	float pad0;

	vec3 vmax; // extent: 0.5f * (vmax - vmin)
	float pad1;

	vec3 origin;
	float pad2;

	vec3 extent;
	float sphereRadius;
};

struct SceneData {
    mat4 view;
    mat4 proj;
    mat4 viewproj;
    vec4 ambientColor;
    vec4 sunlightDirection; // .w = power
    vec4 sunlightColor;
    vec4 cameraPos;
    vec4 envMapIndex; // x = diffuse, y = specular, z = brdf, w = skybox
};

struct Vertex {
    vec3 position;
    float pad0;

    vec2 uv;
    vec2 pad1;

    vec3 normal;
    float pad2;

    vec4 color;
    uint pad3[4];
};

struct Material {
    vec4 colorFactor;

    vec2 metalRoughFactors;
    vec2 pad0;

	uint albedoLUTIndex;
    uint metalRoughLUTIndex;
	uint normalLUTIndex;
	uint aoLUTIndex;

    vec3 emissiveColor;
    float emissiveStrength;

    float ambientOcclusion;
    float normalScale;
    float alphaCutoff;
    float pad3;
};

struct Instance {
    mat4 modelMatrix;
    AABB localAABB;
    uint materialIndex;
    uint drawRangeIndex;
    uint pad0[2];

    uint64_t vertexAddress;
    uint64_t indexAddress;

    uint pad1[4];
};

struct GPUAddressTable {
    uint64_t instanceBuffer;
    uint64_t indirectCmdBuffer;
    uint64_t drawRangeBuffer;
    uint64_t materialBuffer;
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
    int  vertexOffset;
    uint firstInstance;

    uint instanceIndex;
    uint drawOffset;
    uint pad0;
};

struct DrawPushConstants {
    uint materialIndex;
    uint64_t vertexAddress;
    uint64_t indexAddress;
    mat4 modelMatrix;
    uint drawRangeIndex;
};

layout(buffer_reference, std430) readonly buffer IndirectBuffer {
    IndirectDrawCmd cmds[];
};

layout(buffer_reference, std430) readonly buffer DrawRangeBuffer {
    GPUDrawRange ranges[];
};

layout(buffer_reference, std430) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(buffer_reference, std430) readonly buffer IndexBuffer {
    uint indices[];
};

layout(buffer_reference, std430) readonly buffer MaterialBuffer {
    Material materials[];
};

layout(buffer_reference, std430, scalar) readonly buffer InstanceBuffer {
    Instance instances[];
};

#endif