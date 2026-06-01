#ifndef COMMON_GLSL
#define COMMON_GLSL

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_ARB_gpu_shader_int64 : require

#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require

#extension GL_EXT_shader_8bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require

#extension GL_EXT_nonuniform_qualifier : require

const float PI      = 3.1415926535897932384626433832795;
const float HALF_PI = 1.5707963267948966192313216916398;

const uint MAX_ENV_SETS = 8u;

const uint AA_OFF   = 0u;
const uint AA_CMAA2 = 1u;
const uint AA_SMAA  = 2u;
const uint AA_FXAA  = 3u;
const uint AA_TAA   = 4u;

const uint TM_ACESFILM = 0u;
const uint TM_GT7      = 1u;

const uint AO_OFF               = 0u;
const uint AO_VBAO              = 1u;
const uint AO_VBAO_BENT_NORMALS = 2u;

const uint PASS_OPAQUE      = 0u;
const uint PASS_TRANSPARENT = 1u;

const uint MATERIAL_FLAG_ALPHA_MASKED   = (1u << 0u);
const uint MATERIAL_FLAG_CASTS_SHADOWS  = (1u << 1u);
const uint MATERIAL_FLAG_HAS_NORMAL_MAP = (1u << 2u);
const uint MATERIAL_FLAG_IS_TREE        = (1u << 3u);

const uint MAX_LUMINANCE_GROUPS = 65536u;

const float GTAO_RADIUS_MULTIPLIER = 1.457;

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
const uint PUSH_BINDING_WRITE_1  = 7u;
const uint PUSH_BINDING_WRITE_2  = 8u;
const uint PUSH_BINDING_WRITE_3  = 9u;
const uint PUSH_BINDING_WRITE_4  = 10u;
const uint PUSH_BINDING_WRITE_5  = 11u;

// =================================
// === ADDRESS TABLE BUFFER IDS  ===
// =================================

const uint ABT_VisibleInstances  = 0u;
const uint ABT_IndirectDraws     = 1u;

const uint ABT_VisibleLightCount  = 2u;
const uint ABT_VisibleLightIDs    = 3u;
 
const uint ABT_ClusterCounts           = 4u;
const uint ABT_ClusterOffsets          = 5u;
const uint ABT_ClusterCursors          = 6u;
const uint ABT_ClusterLightIDs         = 7u;
const uint ABT_ClusterTileSliceRanges  = 8u;
const uint ABT_ClusterScanScratch      = 9u;

const uint ABT_Cmaa2Control            = 10u;
const uint ABT_Cmaa2ShapeCandidates    = 11u;
const uint ABT_Cmaa2DeferredLocations  = 12u;
const uint ABT_Cmaa2DeferredItems      = 13u;
const uint ABT_Cmaa2DeferredHeads      = 14u;

const uint ABT_DispatchIndirectArgs    = 15u;

const uint ABT_Lights                  = 16u;
const uint ABT_Transforms              = 17u;
const uint ABT_PrevTransforms          = 18u;
const uint ABT_Material                = 19u;
const uint ABT_Mesh                    = 20u;
const uint ABT_Vertex                  = 21u;
const uint ABT_Index                   = 22u;
const uint ABT_Luminance               = 23u;

const uint ABT_Count                   = 24u;

const uint INDIRECT_DISPATCH_SLOT_LIGHTS          = 0u;  // args[0]
const uint INDIRECT_DISPATCH_SLOT_CLUSTERS        = 1u;  // args[1]
const uint INDIRECT_DISPATCH_SLOT_CMAA2_SHAPES    = 2u;  // args[2]
const uint INDIRECT_DISPATCH_SLOT_CMAA2_DEFERRED  = 3u;  // args[3]

// All defined as VkDrawIndexedIndirectCommand
const uint DRAW_STATIC        = 0u;
const uint DRAW_MULTI_STATIC  = 1u;
const uint DRAW_DYNAMIC       = 2u;
const uint DRAW_MULTI_DYNAMIC = 3u;

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
	mat4 prevView;
	mat4 viewProjUnjittered;
	uvec4 temporal;           // .x = frameIndex, .y = historyValid (0/1), z = Hi-Z valid(0/1)
	vec4 temporalJitter;
	// x = current jitter x ndc
	// y = current jitter y
	// z = previous jitter x
	// w = previous jitter y
	vec4 sunlightDirection;   // .w = power
	vec4 sunlightColor;
	vec4 cameraPos;           // xyz pos, .w exposure
	vec4 cameraClips;         // .x near and .y far, .z invScreenWidth, .w invScreenHeight
	vec4 viewportSize;        // .x and .y for width and height, .z for pixel count
	vec4 pixelSizes;          // .x/.y = 1 / full extent .z/.w = = 1 / half extent
};

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

struct Instance {
	uint instanceID;
	uint meshID;
	uint materialID;
	uint transformID;
	uint passType;
};

struct AABB {
	vec3 vmin; // origin: 0.5f * (vmin + vmax)
	vec3 vmax; // extent: 0.5f * (vmax - vmin)
};

struct Material
{
	vec4 colorFactor;
	vec2 metalRoughFactors;

	vec3 emissiveColor;
	float emissiveStrength;

	float normalScale;
	float alphaCutoff;
	uint passType;
	uint flags;

	uint albedoID;
	uint metalRoughnessID;
	uint normalID;
	uint emissiveID;
};

struct Mesh
{
	AABB localAABB;
	uint firstIndex;
	uint indexCount;
	uint vertexOffset;
	uint vertexCount;
	uint shadowFirstIndex;
	uint shadowIndexCount;
};

struct Vertex
{
	// half float 6 bytes
	uint16_t  positionX;
	uint16_t  positionY;
	uint16_t  positionZ;

	// oct-encoded 2 bytes
	int8_t    normalX;
	int8_t    normalY;

	// 2 bytes
	int8_t    tangentX; // oct-encoded
	int8_t    tangentY; // sign in MSB (tangentW packed as sign bit)

	// unorm16 4 bytes
	uint16_t  uvX;
	uint16_t  uvY;

	// 4 bytes
	uint colorRGBA8;
};
// Total: 18 bytes

//vec3 unpackPosition(uint xy, uint z_nx)
//{
//	vec2 xy16 = unpackHalf2x16(xy);
//	vec2 zn   = unpackHalf2x16(z_nx); // zn.x = posZ, zn.y = normalX raw (unused here)
//	return vec3(xy16.x, xy16.y, zn.x);
//}
vec3 unpackPosition(Vertex vtx)
{
	uint xy = uint(vtx.positionX) | (uint(vtx.positionY) << 16u);
	uint zw = uint(vtx.positionZ);
	vec3 position = vec3(unpackHalf2x16(xy), unpackHalf2x16(zw).x);
	return position;
}

vec2 octEncode(vec3 n) {
	n /= abs(n.x) + abs(n.y) + abs(n.z);
	if (n.z < 0.0) n.xy = (1.0 - abs(n.yx)) * sign(n.xy);
	return n.xy * 0.5 + 0.5;
}

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

float snorm8ToFloat(int v) { return clamp(float(v) / 127.0, -1.0, 1.0); }

float snorm16ToFloat(int16_t v) {
	// maps [-32767..32767] to [-1..1]
	return clamp(float(v) / 32767.0, -1.0, 1.0);
}

vec2 unpackUV(uint16_t uvX, uint16_t uvY) {
	uint packed = (uint(uvY) << 16u) | uint(uvX);
	return unpackHalf2x16(packed);
}

vec4 unpackRGBA8(uint packedRGBA8) {
	return unpackUnorm4x8(packedRGBA8);
}

float luminance(vec3 color) {
	return max(dot(color, vec3(0.2126, 0.7152, 0.0722)), 1e-4);
}

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

// https://github.com/PanosK92/SpartanEngine/blob/4a0fe6d6d6ae54be08d5e9541a75adfb9d32d35f/data/shaders/common.hlsl#L434
float temporalInterleavedGradientNoise(vec2 screen_pos, int frame_count, float taaOn) {
	const float RPC_16 = 0.0625;

	// temporal factor
	float animate      = saturate(taaOn + 1.0);
	float frame_step   = float(frame_count % 16) * RPC_16 * animate;
	screen_pos.x      += frame_step * 4.7526;
	screen_pos.y      += frame_step * 3.1914;

	vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
	return fract(magic.z * fract(dot(screen_pos, magic.xy)));
}

// fast 1d hash
float hash(float p) {
	// scale input, convert to uint for bit manipulation
	uint u = floatBitsToUint(p * 3141592653.0);
	
	// mix with multiply and xor, normalize to [0,1)
	return float(u * u * 3141592653u) / 4294967295.0;
}

// fast 2d hash
float hash(vec2 p) {
	// scale each component, convert to uint2
	uvec2 u = floatBitsToUint(p * vec2(141421356.0, 2718281828.0));
	
	// combine with xor, mix, normalize to [0,1)
	return float((u.x ^ u.y) * 3141592653u) / 4294967295.0;
}

const uint MAX_CASCADES = 4u;
const uint MAX_CASCADES_INDEX = MAX_CASCADES - 1u;
struct ShadowCSM {
	mat4 cascadeVP[MAX_CASCADES];
	vec4 cascadeSplits;
	// x=bias, y=shadowAtlasID, z=cascadeCount, w=atlasTexelSize(1/atlasHeight)
	vec4 params;
	// xy = uvScale, zw = uvOffset (per cascade)
	vec4 atlasUV[MAX_CASCADES];
	vec4 maxFilterRadiusTexels;
	float cascadeBias[MAX_CASCADES];
	//float cascadeNormalOffset[MAX_CASCADES];
};

// Lighting struct
struct LocalLight {
	uint lightType;

	vec3 position;
	float radius;

	vec3 color;
	float intensity;

	vec3 direction;
	float innerCos;

	float outerCos;
	uint flags;
};

struct ClusteredData {
	uint tileSizeX;
	uint tileSizeY;
	uint zSlices;
	uint maxLightsPerCluster;
	uint tileCountX;
	uint tileCountY;
	uint clusterCount;
	uint maxVisibleLights;
	vec4 pad0[6];
};

struct EnvMapIndexArray {
	uvec4 indices[MAX_ENV_SETS];
	// x = diffuseMapIndex
	// y = specularMapIndex
	// z = brdfLUTIndex
	// w = skyboxMapIndex
};

// inline uniform block
struct DebugToggles
{
	uint enableOBBs;
	uint enableLensFlare;
	uint enableChromaticAberration;
	uint enableSSS;

	uint aaMode;
	uint aoMode;
	uint shadowFilter;
	uint enableShadows;

	uint enableVolumetrics;
	uint tonemapper;
	uint activeEnvMap;
	uint showAlbedo;

	uint showNormals;
	uint showRoughness;
	uint showMetallic;
	uint showAmbientOcclusion;

	uint showSpecular;
	uint showDiffuse;
	uint showEmissive;
	uint showBentNormals;

	uint showCascadeSplits;
	uint showSSS;

	uint enableProfilerView;
	uint enableSettings;
};


// =================================
// === GLOBAL ssbo address table ===
// =================================
layout(set = GLOBAL_SET, binding = ADDRESS_TABLE_BINDING, scalar) readonly buffer GlobalAddressTableBuffer {
	GPUAddressTable globalAddressTable;
};
uint64_t getABTGlobalAddress(uint id) { return globalAddressTable.addrs[id]; }

// ===========================
// === GLOBAL uniform sets ===
// ===========================

layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_DEBUG_INLINE, scalar) uniform DebugData {
	DebugToggles debug;
};

DebugToggles getDebugToggles() { return debug; }


// ============================================
// === GLOBAL ADDRESSS TABLE BUFFER GETTERS ===
// ============================================

// materials, vertices, indices, all ready at render time and uploaded at end of asset loading
layout(buffer_reference, scalar) readonly buffer MaterialBuffer {
	Material materials[];
};
MaterialBuffer getMaterialBuffer() {
	uint64_t addr = getABTGlobalAddress(ABT_Material);
	return MaterialBuffer(addr);
}

layout(buffer_reference, scalar) readonly buffer VertexBuffer {
	Vertex vertices[];
};
VertexBuffer getVertexBuffer() {
	uint64_t addr = getABTGlobalAddress(ABT_Vertex);
	return VertexBuffer(addr);
}

layout(buffer_reference, scalar) readonly buffer IndexBuffer {
	uint indices[];
};
IndexBuffer getIndexBuffer() {
	uint64_t addr = getABTGlobalAddress(ABT_Index);
	return IndexBuffer(addr);
}

layout(buffer_reference, scalar) readonly buffer MeshBuffer {
	Mesh meshes[];
};
MeshBuffer getMeshBuffer() {
	uint64_t addr = getABTGlobalAddress(ABT_Mesh);
	return MeshBuffer(addr);
}

// .x = exposure when applied in final tone map stage
layout(buffer_reference, scalar) buffer LuminanceBuffer {
	vec4 luminanceSums[MAX_LUMINANCE_GROUPS];
};
LuminanceBuffer getLuminanceBuffer() {
	uint64_t addr = getABTGlobalAddress(ABT_Luminance);
	return LuminanceBuffer(addr);
}


// ================================
// === FRAME ssbo address table ===
// =================================

layout(set = FRAME_SET, binding = ADDRESS_TABLE_BINDING, scalar) readonly buffer FrameAddressTableBuffer {
	GPUAddressTable frameAddressTable;
};
uint64_t getABTFrameAddress(uint id) { return frameAddressTable.addrs[id]; }

// ===========================
// === FRAME uniform sets ====
// ===========================
layout(set = FRAME_SET, binding = FRAME_BINDING_SCENE) uniform SceneUBO {
	SceneData scene;
};
SceneData getSceneData() { return scene; }

layout(set = FRAME_SET, binding = FRAME_BINDING_CSM) uniform ShadowUBO {
	ShadowCSM csm;
};
ShadowCSM getShadowCSM() { return csm; }

layout(set = FRAME_SET, binding = FRAME_BINDING_CLUSTERED) uniform ClusteredUBO {
	ClusteredData clusteredData;
} ;
ClusteredData getClusteredData() { return clusteredData; }


// ===========================================
// === FRAME ADDRESSS TABLE BUFFER GETTERS ===
// ===========================================


// instances
layout(buffer_reference, scalar) buffer VisibleInstances {
	Instance instances[];
};
// pass gl_InstanceIndex
VisibleInstances getInstanceBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_VisibleInstances);
	return VisibleInstances(addr);
}

// indirect draws
layout(buffer_reference, scalar) buffer IndirectDraws {
	IndirectDrawCmd indirectDraws[];
};
IndirectDraws getIndirectDrawBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_IndirectDraws);
	return IndirectDraws(addr);
}

layout(buffer_reference, scalar) readonly buffer TransformsBuffer {
	mat4 transforms[];
};
TransformsBuffer getTransformBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_Transforms);
	return TransformsBuffer(addr);
}

layout(buffer_reference, scalar) readonly buffer PrevTransformsBuffer {
	mat4 prevTransforms[];
};
PrevTransformsBuffer getPrevTransformBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_PrevTransforms);
	return PrevTransformsBuffer(addr);
}


// Indirect dispatch arguments
layout(buffer_reference, scalar) buffer DispatchIndirectArgsBuffer {
	uvec4 args[16]; // args[i].xyz used
};
DispatchIndirectArgsBuffer getIndirectDispatchBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_DispatchIndirectArgs);
	return DispatchIndirectArgsBuffer(addr);
}

// =====================
// === CMAA2 buffers ===
// =====================
layout(buffer_reference, scalar) buffer Cmaa2ControlBuffer {
	uint control[];
};
Cmaa2ControlBuffer getCMAA2ControlBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_Cmaa2Control);
	return Cmaa2ControlBuffer(addr);
}

layout(buffer_reference, scalar) buffer Cmaa2ShapeCandidatesBuffer {
	uint pixelIDs[];
};
Cmaa2ShapeCandidatesBuffer getCMAA2ShapeCandidatesBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_Cmaa2ShapeCandidates);
	return Cmaa2ShapeCandidatesBuffer(addr);
}

layout(buffer_reference, scalar) buffer Cmaa2DeferredLocationsBuffer {
	uint quadIDs[];
};
Cmaa2DeferredLocationsBuffer getCMAA2DeferredLocationsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_Cmaa2DeferredLocations);
	return Cmaa2DeferredLocationsBuffer(addr);
}

layout(buffer_reference, scalar) buffer Cmaa2DeferredItemsBuffer {
	uvec4 items[];
};
Cmaa2DeferredItemsBuffer getCMAA2DeferredItemsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_Cmaa2DeferredItems);
	return Cmaa2DeferredItemsBuffer(addr);
}

layout(buffer_reference, scalar) buffer Cmaa2DeferredHeadsBuffer {
	uint heads[];
};
Cmaa2DeferredHeadsBuffer getCMAA2DeferredHeadsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_Cmaa2DeferredHeads);
	return Cmaa2DeferredHeadsBuffer(addr);
}

// =================================
// === clustered shading buffers ===
// =================================

layout(buffer_reference, scalar) readonly buffer LightBuffer {
	LocalLight lights[];
};
LightBuffer getLightBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_Lights);
	return LightBuffer(addr);
}

layout(buffer_reference, scalar) buffer VisibleLightCount {
	uint count;
};
VisibleLightCount getVisibleLightCountBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_VisibleLightCount);
	return VisibleLightCount(addr);
}

layout(buffer_reference, scalar) buffer VisibleLightIDs {
	uint ids[];
};
VisibleLightIDs getVisibleLightIDsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_VisibleLightIDs);
	return VisibleLightIDs(addr);
}

layout(buffer_reference, scalar) buffer ClusterCounts {
	uint counts[];
};
ClusterCounts getClusterCountsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_ClusterCounts);
	return ClusterCounts(addr);
}

layout(buffer_reference, scalar) buffer ClusterOffsets {
	uint offsets[];
};
ClusterOffsets getClusterOffsetsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_ClusterOffsets);
	return ClusterOffsets(addr);
}

layout(buffer_reference, scalar) buffer ClusterCursors {
	uint cursors[];
};
ClusterCursors getClusterCursorsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_ClusterCursors);
	return ClusterCursors(addr);
}

layout(buffer_reference, scalar) buffer ClusterLightIDs {
	uint lightIDs[];
};
ClusterLightIDs getClusterLightIDsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_ClusterLightIDs);
	return ClusterLightIDs(addr);
}

// One per tile: (minSlice, maxSlice)
layout(buffer_reference, scalar) buffer ClusterTileSliceRanges {
	uvec2 ranges[];
};
ClusterTileSliceRanges getClusterTileSliceRangesBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_ClusterTileSliceRanges);
	return ClusterTileSliceRanges(addr);
}

// Scratch for scan
layout(buffer_reference, scalar) buffer ClusterScanScratch {
	uint scratch[];
};
ClusterScanScratch getClusterScratchBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_ClusterScanScratch);
	return ClusterScanScratch(addr);
}



// ==============================
// === GLOBAL BINDLESS IMAGES ===
// ==============================
layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_SAMPLER_CUBE)
uniform samplerCube envMaps[];

layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_COMBINED_SAMPLER)
uniform sampler2D combinedSamplers[];

layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_COMBINED_SAMPLER)
uniform usampler2D combinedSamplersU[];

#define TEX2D(id) combinedSamplers[nonuniformEXT(id)]
#define TEXU2D(id) combinedSamplersU[nonuniformEXT(id)]
#define TEXCUBE(id) envMaps[nonuniformEXT(id)]

#define INVALID_TEXTURE_ID 0xFFFFFFFFu

vec4 SampleTexture(uint id, vec2 uv) {
	if (id == INVALID_TEXTURE_ID) {
		return vec4(1.0);
	}
	return texture(TEX2D(id), uv);
}

uvec4 SampleTexelFetch(uint id, ivec2 uv, int lod) {
	if (id == INVALID_TEXTURE_ID) {
		return uvec4(1u);
	}
	return texelFetch(TEXU2D(id), uv, lod);
}

vec4 SampleTextureLod(uint id, vec2 uv, float lod) {
	if (id == INVALID_TEXTURE_ID) {
		return vec4(1.0);
	}
	return textureLod(TEX2D(id), uv, lod);
}

vec4 SampleTextureGrad(uint id, vec2 uv, vec2 dx, vec2 dy) {
	if (id == INVALID_TEXTURE_ID) {
		return vec4(1.0);
	}
	return textureGrad(TEX2D(id), uv, dx, dy);
}

vec4 SampleCube(uint id, vec3 dir) {
	if (id == INVALID_TEXTURE_ID) {
		return vec4(0.0);
	}
	return texture(TEXCUBE(id), dir);
}

vec4 SampleCubeLod(uint id, vec3 dir, float lod) {
	if (id == INVALID_TEXTURE_ID) {
		return vec4(0.0);
	}
	return textureLod(TEXCUBE(id), dir, lod);
}

int SampleCubeQueryLevels(uint id) {
	if (id == INVALID_TEXTURE_ID) {
		return 0;
	}
	return textureQueryLevels(TEXCUBE(id));
}

void unpackVertex(
	int id,
	out vec2 uv,
	out vec4 color,
	out vec3 normal,
	out vec3 tangent,
	out float tangentHandedness,
	out vec3 position)
{
	Vertex vtx = getVertexBuffer().vertices[id];

	uint xy = uint(vtx.positionX) | (uint(vtx.positionY) << 16u);
	uint zw = uint(vtx.positionZ);
	position = vec3(unpackHalf2x16(xy), unpackHalf2x16(zw).x);

	uv    = unpackUV(vtx.uvX, vtx.uvY);
	color = unpackUnorm4x8(vtx.colorRGBA8);

	normal = octDecode(vec2(snorm8ToFloat(int(vtx.normalX)), snorm8ToFloat(int(vtx.normalY))));

	int ty = int(vtx.tangentY);
	tangentHandedness = (ty >= 0) ? 1.0 : -1.0;
	tangent = octDecode(vec2(snorm8ToFloat(int(vtx.tangentX)), snorm8ToFloat(abs(ty))));
}

void unpackVertexPrepass(
	int id,
	out vec2 uv,
	out vec3 position,
	out vec3 normal)
{
	Vertex vtx = getVertexBuffer().vertices[id];

	uint xy = uint(vtx.positionX) | (uint(vtx.positionY) << 16u);
	uint zw = uint(vtx.positionZ);
	position = vec3(unpackHalf2x16(xy), unpackHalf2x16(zw).x);

	uv     = unpackUV(vtx.uvX, vtx.uvY);
	normal = octDecode(vec2(snorm8ToFloat(int(vtx.normalX)), snorm8ToFloat(int(vtx.normalY))));
}

#endif
