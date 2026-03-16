#ifndef GPU_SCENE_STRUCTURES_GLSL
#define GPU_SCENE_STRUCTURES_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require

// debug helpers
#define DBG(x) (debug.x != 0u)
#define RET(rgb, a) { outFragColor = vec4((rgb), (a)); return; }

struct SceneData {
	mat4 view;
	mat4 proj;
	mat4 invView;
	mat4 invProj;
	mat4 viewProj;
	mat4 prevViewProj;
	uvec4 temporal;         // .x = frameIndex, .y = historyValid (0/1), z = Hi-Z valid(0/1)
	vec4 sunlightDirection; // .w = power
	vec4 sunlightColor;
	vec4 cameraPos;         // .z camFar
	vec4 cameraClips;       // .x near and .y far, .z invScreenWidth, .w invScreenHeight
	vec4 viewportSize;      // .x and .y for width and height, .z for pixel count
	vec4 pixelSizes;        // .x/.y = 1 / full extent .z/.w = = 1 / half extent
	vec4 pad0;
};

// Number of env sets stored in the buffer (must match C++ side)
const uint MAX_ENV_SETS = 8u;

struct EnvMapIndexArray {
	uvec4 indices[MAX_ENV_SETS];
	// x = diffuseMapIndex
	// y = specularMapIndex
	// z = brdfLUTIndex
	// w = skyboxMapIndex
};

// inline uniform block
struct DebugToggles {
	// Higher level toggles
	uint enableOBBs;
	uint enableenableProfilerView;
	uint enableSettings;
	uint aaMode; // 0 off, 1 cmaa2, 2 smaa, 3 fxaa

	uint aoMode; // 0 off, 1 gtao
	uint enableShadows;
	uint enableVolumetrics;
	uint activeEnvMap; // Indexes environment indices

	// draw stats
	uint meshCount;
	uint materialCount;
	uint transformCount;
	uint vertexCount;

	uint indexCount;
	uint enableLensFlare;
	uint enableChromaticAberration;
	uint enableSSS;

	// fragment shader outputs
	uint showAlbedo;
	uint showNormals;
	uint showRoughness;
	uint showMetallic;

	uint showAmbientOcclusion;
	uint showSpecular;
	uint showDiffuse;
	uint showCascadeSplits;

	uint showEmissive;
	uint showDiffuseBounceLight;
	uint enableTemporal;
	uint showSSS; // Screen space contact shadows
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
	int16_t normalX;
	int16_t normalY;
	uint16_t uvX;
	uint16_t uvY;
	uint colorRGBA8;
};

vec3 octDecode(vec2 e) {
	vec3 v = vec3(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));

	if (v.z < 0.0) {
		vec2 signNotZero = vec2(
			(v.x >= 0.0) ? 1.0 : -1.0,
			(v.y >= 0.0) ? 1.0 : -1.0
		);
		v.xy = (vec2(1.0) - abs(v.yx)) * signNotZero;
	}

	return normalize(v);
}

float snorm16ToFloat(int16_t v) {
	// maps [-32767..32767] to [-1..1]
	return clamp(float(v) / 32767.0, -1.0, 1.0);
}

vec2 unpackUV(uint16_t uvX, uint16_t uvY) {
	uint packed = (uint(uvY) << 16) | uint(uvX);
	return unpackHalf2x16(packed);
}

vec4 unpackRGBA8(uint packedRGBA8) {
	return unpackUnorm4x8(packedRGBA8);
}

const uint PASS_OPAQUE      = 0u;
const uint PASS_TRANSPARENT = 1u;

const uint MATERIAL_FLAG_ALPHA_MASKED = 1u << 0;
const uint MATERIAL_FLAG_CASTS_SHADOWS = 1u << 1;
const uint MATERIAL_FLAG_HAS_NORMAL_MAP = 1u << 2;
struct Material {
	vec4 colorFactor;
	vec2 metalRoughFactors;

	float ambientOcclusion;
	float normalScale;

	vec3 emissiveColor;
	float emissiveStrength;

	float alphaCutoff;
	uint passType;
	uint flags;

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
	uint shadowFirstIndex;
	uint shadowIndexCount;
};

// All defined as VkDrawIndexedIndirectCommand
const uint DRAW_STATIC        = 0u;
const uint DRAW_MULTI_STATIC  = 1u;
const uint DRAW_DYNAMIC       = 2u;
const uint DRAW_MULTI_DYNAMIC = 3u;

struct Instance {
	uint instanceID;
	uint meshID;
	uint materialID;
	uint transformID;
	uint passType;
};

const uint ABT_VisibleInstances              = 0u;
const uint ABT_IndirectDraws                 = 1u;

// Buffers stored in clustered.glsl
const uint ABT_VisibleLightCount             = 2u;
const uint ABT_VisibleLightIDs               = 3u;

const uint ABT_ClusterCounts                 = 4u;
const uint ABT_ClusterOffsets                = 5u;
const uint ABT_ClusterCursors                = 6u;
const uint ABT_ClusterLightIDs               = 7u;
const uint ABT_ClusterTileSliceRanges        = 8u;
const uint ABT_ClusterScanScratch            = 9u;

const uint ABT_Cmaa2Control                  = 10u;
const uint ABT_Cmaa2ShapeCandidates          = 11u;
const uint ABT_Cmaa2DeferredLocations        = 12u;
const uint ABT_Cmaa2DeferredItems            = 13u;
const uint ABT_Cmaa2DeferredHeads            = 14u;

const uint ABT_DispatchIndirectArgs          = 15u;

const uint ABT_Lights                        = 16u;
const uint ABT_Transforms                    = 17u;
const uint ABT_PrevTransforms                = 18u;
const uint ABT_Material                      = 19u;
const uint ABT_Mesh                          = 20u;
const uint ABT_Vertex                        = 21u;
const uint ABT_Index                         = 22u;
const uint ABT_Luminance                     = 23u;

const uint ABT_Count                         = 24u;


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

layout(buffer_reference, scalar) readonly buffer PrevTransformsBuffer {
	mat4 prevTransforms[];
};

layout(buffer_reference, scalar) readonly buffer MeshBuffer {
	Mesh meshes[];
};

const uint MAX_LUMINANCE_GROUPS = 65536u;
// .x = exposure when applied in final tone map stage
layout(buffer_reference, scalar) buffer LuminanceBuffer {
	vec4 luminanceSums[MAX_LUMINANCE_GROUPS];
};

const uint INDIRECT_DISPATCH_SLOT_LIGHTS         = 0u;  // args[0]
const uint INDIRECT_DISPATCH_SLOT_CLUSTERS       = 1u;  // args[1]
const uint INDIRECT_DISPATCH_SLOT_CMAA2_SHAPES   = 2u;  // args[2]
const uint INDIRECT_DISPATCH_SLOT_CMAA2_DEFERRED = 3u;  // args[3]

layout(buffer_reference, scalar) buffer DispatchIndirectArgsBuffer {
	uvec4 args[16]; // args[i].xyz used
};

float saturate(float value) { return clamp(value, 0.0, 1.0); }
vec2 saturate(vec2 value)  { return clamp(value, vec2(0.0), vec2(1.0)); }
vec4 saturate(vec4 value)  { return clamp(value, vec4(0.0), vec4(1.0)); }

float mad(float a, float b, float c) { return a * b + c; }
vec2 mad(vec2 a, vec2 b, vec2 c)   { return a * b + c; }
vec3 mad(vec3 a, vec3 b, vec3 c)   { return a * b + c; }
vec4 mad(vec4 a, vec4 b, vec4 c)   { return a * b + c; }
vec2 mad(float a, vec2 b, vec2 c)  { return a * b + c; }

void SMAAMovc(bvec2 cond, inout vec2 variable, vec2 value) {
	if (cond.x) variable.x = value.x;
	if (cond.y) variable.y = value.y;
}

void SMAAMovc(bvec4 cond, inout vec4 variable, vec4 value) {
	SMAAMovc(cond.xy, variable.xy, value.xy);
	SMAAMovc(cond.zw, variable.zw, value.zw);
}

float interleavedGradientNoise(vec2 pixel) {
	const vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
	return fract(magic.z * fract(dot(pixel, magic.xy)));
}

#endif
