#ifndef COMMON_GLSL
#define COMMON_GLSL

#extension GL_GOOGLE_include_directive : require

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_ARB_gpu_shader_int64 : require

#extension GL_EXT_shader_16bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types_int16 : require

#extension GL_EXT_shader_8bit_storage : require
#extension GL_EXT_shader_explicit_arithmetic_types_int8 : require

#extension GL_EXT_nonuniform_qualifier : require

#include "bindings.glsl"
#include "instances.glsl"
#include "draws.glsl"

const float PI      = 3.1415926535897932384626433832795;
const float HALF_PI = 1.5707963267948966192313216916398;

const uint MAX_CASCADES = 4u;
const uint MAX_CASCADES_INDEX = MAX_CASCADES - 1u;

const uint MAX_ENV_SETS = 8u;

const uint AA_OFF   = 0u;
const uint AA_CMAA2 = 1u;
const uint AA_SMAA  = 2u;
const uint AA_FXAA  = 3u;
const uint AA_TAA   = 4u;

//const uint TM_ACESFILM = 0u;
//const uint TM_GT7      = 1u;

const uint AO_OFF               = 0u;
const uint AO_VBAO              = 1u;
const uint AO_VBAO_BENT_NORMALS = 2u;

const uint MAX_LUMINANCE_GROUPS = 65536u;

const float GTAO_RADIUS_MULTIPLIER = 1.457;

// debug helpers
#define DBG(x) (debug.x != 0u)
#define RET(rgb, a) { outFragColor = vec4((rgb), (a)); return; }

struct GPUStats
{
	uint visibleOpaque;

	uint visibleTransparent;

	uint visibleShadowCasters;

	uint opaqueDrawCount;
	uint transparentDrawCount;
	uint shadowDrawCount;

	uint triangleCount; // Doesnt count shadows
};

struct SceneData
{
	mat4 view;
	mat4 proj;
	mat4 projUnjittered;
	mat4 invView;
	mat4 invProj;
	mat4 viewProj;
	mat4 prevViewProj;
	mat4 prevView;
	mat4 viewProjUnjittered;
	uvec4 temporal;           // .x = frameNumber, .y = historyValid (0/1), z = Hi-Z valid(0/1)
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
	mat4 flashlightVP;
};

struct GPUAddressTable
{
	uint64_t addrs[ABT_Count];
};

struct AABB {
	vec3 vmin; // origin: 0.5f * (vmin + vmax)
	vec3 vmax; // extent: 0.5f * (vmax - vmin)
};

struct Material
{
	vec4 colorFactor;

	vec2 metalRoughFactors;
	float normalScale;
	float alphaCutoff;

	vec3 emissiveColor;
	float emissiveStrength;

	uint albedoID;
	uint metalRoughnessID;
	uint normalID;
	uint emissiveID;
};

struct Mesh
{
	AABB localAABB;
	float localBoundingRadius;
	uint firstIndex;
	uint indexCount;
	uint vertexOffset;
	uint vertexCount;
	uint shadowFirstIndex;
	uint shadowIndexCount;
};

struct Vertex
{
	vec3 position;

	// Normal: octahedral, snorm16
	int16_t   normalX;
	int16_t   normalY;

	// Tangent: octahedral, snorm16. Handedness (W) = sign of tangentY.
	int16_t   tangentX;
	int16_t   tangentY;

	// UV: FP16 bits (half-float)
	uint16_t  uvX;
	uint16_t  uvY;

	// Vertex color, RGBA8 packed
	uint      colorRGBA8;
};

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

float snorm16ToFloat(int v) { return clamp(float(v) / 32767.0, -1.0, 1.0); }

vec2 unpackUV(uint16_t uvX, uint16_t uvY) {
	uint packed = (uint(uvY) << 16u) | uint(uvX);
	return unpackHalf2x16(packed);
}

vec4 unpackRGBA8(uint packedRGBA8) {
	return unpackUnorm4x8(packedRGBA8);
}

AABB TransformAABB(AABB localAABB, mat4 transform)
{
	AABB worldAABB;

	vec3 center = 0.5 * (localAABB.vmin + localAABB.vmax);
	vec3 extent = 0.5 * (localAABB.vmax - localAABB.vmin);

	vec3 worldCenter = (transform * vec4(center, 1.0)).xyz;

	mat3 rotationScale = mat3(transform);
	mat3 absMatrix = mat3(
		abs(rotationScale[0]),
		abs(rotationScale[1]),
		abs(rotationScale[2]));

	vec3 worldExtent = absMatrix * extent;

	worldAABB.vmin = worldCenter - worldExtent;
	worldAABB.vmax = worldCenter + worldExtent;

	return worldAABB;
}

float luminance(vec3 color) {
	return max(dot(color, vec3(0.2126, 0.7152, 0.0722)), 1e-5);
}

// helper: karis average
// suppresses fireflies by weighting bright pixels less
float KarisWeight(vec3 color)
{
	return 1.0 / (1.0 + luminance(color));
}

float softThreshold(float value, float threshold, float knee)
{
	float kneeMin = threshold - knee;
	float kneeMax = threshold + knee;

	if (value <= kneeMin) return 0.0;

	if (value >= kneeMax) return value - threshold;

	// Smooth in-between
	float t = (value - kneeMin) / max(kneeMax - kneeMin, 1e-6);
	t = t * t * (3.0 - 2.0 * t);

	float soft = (value - threshold) * t;
	return max(soft, 0.0);
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

mat2 createHash(vec2 pixelCoord)
{
	float ang = interleavedGradientNoise(pixelCoord) * 6.2831853;
	return mat2(cos(ang), -sin(ang), sin(ang), cos(ang));
}

mat2 createHashTemporal(vec2 pixelCoord, uint frameIndex)
{
	vec2 jitteredPixel = pixelCoord + float(frameIndex) * vec2(1.618033988, 1.324717957);
	float ang = interleavedGradientNoise(jitteredPixel) * 6.2831853;
	return mat2(cos(ang), -sin(ang), sin(ang), cos(ang));
}

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

struct ShadowCSM {
	mat4 cascadeVP[MAX_CASCADES];
	mat4 cascadeLightViews[MAX_CASCADES];
	vec4 cascadeSplits;
	// x=shadowAtlasID, y=cascadeCount, z=atlasTexelSize(1/atlasHeight)
	vec4 params;
	// xy = uvScale, zw = uvOffset (per cascade)
	vec4 atlasUV[MAX_CASCADES];
	vec4 maxFilterRadiusTexels;
	vec4 cascadeWorldTexels;
};

struct ShadowCullData {
	vec4 receiverLSMin[MAX_CASCADES];   // xyz used, w = pad
	vec4 receiverLSMax[MAX_CASCADES];

	// per-cascade active flag — 0 means no visible receivers, skip entirely
	uvec4 cascadeActive;                // .x=c0 .y=c1 .z=c2 .w=c3
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

// inline uniform block
struct DebugToggles
{
	uint enableLensFlare;
	uint enableChromaticAberration;
	uint enableSSS;
	uint enableFlashlight;

	uint aaMode;
	uint aoMode;
	uint enableShadows;
	uint enableVolumetrics;

	uint activeEnvMap;
	uint disableOcclusionCull;
	uint pad0;
	uint pad1;

	uint enableProfilerView;
	uint enableSettings;
	uint enableBloom;
	float bloomIntensity;

	uint showOpaqueOBBs;
	uint showTransparentOBBs;
	uint activeInstanceCount;
	uint activeLightCount;
};

bool uintBool(uint x) { return x != 0u; }


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

// instance inputs
layout(buffer_reference, scalar) readonly buffer InstanceInputsBuffer {
	InstanceInput instanceInputs[];
};
// pass gl_InstanceIndex
InstanceInputsBuffer getInstanceInputBuffer() {
	uint64_t addr = getABTGlobalAddress(ABT_InstanceInputs);
	return InstanceInputsBuffer(addr);
}

layout(buffer_reference, scalar) readonly buffer DrawBinKeysBuffer {
	BinKey keys[BIN_TABLE_SIZE];
	uvec2  dense[MAX_DRAW_BINS];
};
DrawBinKeysBuffer getDrawBinKeyBuffer() {
	return DrawBinKeysBuffer(getABTGlobalAddress(ABT_DrawBinKeys));
}

uint binLookup(uint meshID, uint materialID)
{
	uint h = (meshID * 2654435761u) ^ (materialID * 2246822519u);
	h &= (BIN_TABLE_SIZE - 1u);

	// open addressing, table built CPU-side so termination is guaranteed
	for (uint probe = 0u; probe < 64u; ++probe)
	{
		BinKey key = getDrawBinKeyBuffer().keys[h];
		if (key.meshID == meshID && key.materialID == materialID) return key.binID;
		if (key.meshID == INVALID_U32) return INVALID_U32; // unregistered pair
		h = (h + 1u) & (BIN_TABLE_SIZE - 1u);
	}
	return INVALID_U32;
}

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

// visible instances
layout(buffer_reference, scalar) buffer VisibleInstanceBuffer {
	VisibleInstance visibles[];
};
VisibleInstanceBuffer getVisibleInstanceBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_VisibleInstances);
	return VisibleInstanceBuffer(addr);
}

// indirect draws
layout(buffer_reference, scalar) buffer IndirectDrawBuffer {
	IndirectIndexedDrawCmd indirectDraws[];
};
IndirectDrawBuffer getIndirectDrawBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_IndirectDraws);
	return IndirectDrawBuffer(addr);
}

// pass gl_InstanceIndex
layout(buffer_reference, scalar) buffer DrawInstanceIDsBuffer {
	uint ids[];
};
DrawInstanceIDsBuffer getDrawInstanceIDsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_DrawInstanceIDs);
	return DrawInstanceIDsBuffer(addr);
}

// draw bin
layout(buffer_reference, scalar) buffer DrawBinBuffer {
	DrawBin drawBins[];
};
DrawBinBuffer getDrawBinBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_DrawBins);
	return DrawBinBuffer(addr);
}

// draw bin counter
layout(buffer_reference, scalar) buffer DrawBinCounterBuffer {
	uint drawBinCounters[];
};
DrawBinCounterBuffer getDrawBinCounterBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_DrawBinCounters);
	return DrawBinCounterBuffer(addr);
}

struct VisibilityCounters
{
	uint counts[VIS_SLOT_COUNT];
};

// instance visibility counters
layout(buffer_reference, scalar) buffer InstanceVisibilityBuffer {
	VisibilityCounters counters;
};
InstanceVisibilityBuffer getInstanceVisibilityBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_InstanceVisibility);
	return InstanceVisibilityBuffer(addr);
}

layout(buffer_reference, scalar) buffer InstanceCursorsBuffer {
	InstanceCursors cursors;
};
InstanceCursorsBuffer getInstanceCursorsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_InstanceCursors);
	return InstanceCursorsBuffer(addr);
}

// instance visible count
layout(buffer_reference, scalar) buffer VisibleCountBuffer {
	uint count;
};
VisibleCountBuffer getVisibleCountBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_VisibleCount);
	return VisibleCountBuffer(addr);
}

layout(buffer_reference, scalar) buffer InstanceStreamsBuffer {
	StreamEntry entries[];
};
InstanceStreamsBuffer getInstanceStreamsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_InstanceStreams);
	return InstanceStreamsBuffer(addr);
}

layout(buffer_reference, scalar) buffer IndirectDrawCountsBuffer {
	uint counts[VIS_SLOT_COUNT];
};
IndirectDrawCountsBuffer getIndirectDrawCountsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_IndirectDrawCounts);
	return IndirectDrawCountsBuffer(addr);
}

bool hasPrimaryVisibles()
{
	VisibilityCounters c = getInstanceVisibilityBuffer().counters;
	return (c.counts[VIS_SLOT_OPAQUE] + c.counts[VIS_SLOT_TRANSPARENT]) > 0u;
}

bool hasOpaqueVisibles()
{
	return getInstanceVisibilityBuffer().counters.counts[VIS_SLOT_OPAQUE] > 0u;
}

bool hasCSMCasters()
{
	VisibilityCounters c = getInstanceVisibilityBuffer().counters;
	return (c.counts[VIS_SLOT_CSM0] +
			c.counts[VIS_SLOT_CSM1] +
			c.counts[VIS_SLOT_CSM2] +
			c.counts[VIS_SLOT_CSM3]) > 0u;
}

bool hasCascadeCasters(uint cascadeSlot)
{
	return getInstanceVisibilityBuffer().counters.counts[cascadeSlot] > 0u;
}

bool hasFlashlightCasters()
{
	return getInstanceVisibilityBuffer().counters.counts[VIS_SLOT_FLASHLIGHT] > 0u;
}

bool hasAnyVisibles()
{
	return getVisibleCountBuffer().count > 0u;
}


layout(buffer_reference, scalar) buffer ShadowCullDataBuffer {
	ShadowCullData data;
};
ShadowCullDataBuffer getShadowCullDataBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_ShadowCullData);
	return ShadowCullDataBuffer(addr);
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

// gpu stats

layout(buffer_reference, scalar) buffer GPUStatsBuffer {
	GPUStats gpuStats;
};
GPUStatsBuffer getGPUStatsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_DrawStats);
	return GPUStatsBuffer(addr);
}


// Indirect dispatch arguments
layout(buffer_reference, scalar) buffer DispatchIndirectArgsBuffer {
	uvec4 args[INDIRECT_DISPATCH_SLOT_COUNT]; // args[i].xyz used
};
DispatchIndirectArgsBuffer getIndirectDispatchBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_DispatchIndirectArgs);
	return DispatchIndirectArgsBuffer(addr);
}

layout(buffer_reference, scalar) buffer DebugCountersBuffer {
	DebugCounters counters;
};
DebugCountersBuffer getDebugCountersBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_DebugCounts);
	return DebugCountersBuffer(addr);
}

layout(buffer_reference, scalar) buffer DebugItemsBuffer {
	DebugItem items[];
};
DebugItemsBuffer getDebugItemsBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_DebugItems);
	return DebugItemsBuffer(addr);
}

layout(buffer_reference, scalar) buffer DebugVertexBuffer {
	DebugVertex vertices[];
};
DebugVertexBuffer getDebugVertexBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_DebugVertex);
	return DebugVertexBuffer(addr);
}

layout(buffer_reference, scalar) buffer DebugDrawBuffer {
	IndirectDrawCmd draw;
};
DebugDrawBuffer getDebugDrawBuffer() {
	uint64_t addr = getABTFrameAddress(ABT_DebugDraw);
	return DebugDrawBuffer(addr);
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

vec4 SampleTextureBias(uint id, vec2 uv, float bias) {
	if (id == INVALID_TEXTURE_ID) {
		return vec4(1.0);
	}
#if defined(GL_FRAGMENT_SHADER) || defined(FRAGMENT_SHADER)
	return texture(TEX2D(id), uv, bias);
#else
	return textureLod(TEX2D(id), uv, 0.0);
#endif
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
	int vertexId,
	out vec2 uv,
	out vec4 color,
	out vec3 normal,
	out vec3 tangent,
	out float tangentHandedness,
	out vec3 position)
{
	Vertex vtx = getVertexBuffer().vertices[vertexId];
	position = vtx.position;

	uv    = unpackUV(vtx.uvX, vtx.uvY);
	color = unpackRGBA8(vtx.colorRGBA8);

	normal = octDecode(vec2(snorm16ToFloat(int(vtx.normalX)),
							snorm16ToFloat(int(vtx.normalY))));

	int ty = int(vtx.tangentY);
	tangentHandedness = (ty >= 0) ? 1.0 : -1.0;
	tangent = octDecode(vec2(snorm16ToFloat(int(vtx.tangentX)),
							snorm16ToFloat(abs(ty))));
}

void unpackVertexPrepass(
	int vertexId,
	out vec2 uv,
	out vec3 position,
	out vec3 normal)
{
	Vertex vtx = getVertexBuffer().vertices[vertexId];
	position = vtx.position;

	uv     = unpackUV(vtx.uvX, vtx.uvY);
	normal = octDecode(vec2(snorm16ToFloat(int(vtx.normalX)),
							snorm16ToFloat(int(vtx.normalY))));
}

#endif
