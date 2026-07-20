#ifndef DRAWS_GLSL
#define DRAWS_GLSL

#extension GL_GOOGLE_include_directive  : require
#include "bindings.glsl"

const uint MAX_DRAW_BINS            = 16384u;  // unique {mesh,material} pairs, all LOD variants
const uint MAX_INSTANCES_PER_STREAM = 262144u;  // size streams + final instance regions
const uint BIN_TABLE_SIZE           = 32768u;  // hash table, power of 2, ~2x MAX_DRAW_BINS
const uint INVALID_U32              = 0xFFFFFFFFu;

const uint DRAW_MAX_OPAQUE      = 32768u;
const uint DRAW_MAX_TRANSPARENT = 8192u;
const uint DRAW_MAX_FLASHLIGHT  = 4096u;
const uint DRAW_MAX_CSM0        = 4096u;
const uint DRAW_MAX_CSM1        = 4096u;
const uint DRAW_MAX_CSM2        = 4096u;
const uint DRAW_MAX_CSM3        = 4096u;

const uint DRAW_OFFSET_OPAQUE      = 0u;
const uint DRAW_OFFSET_TRANSPARENT = DRAW_OFFSET_OPAQUE      + DRAW_MAX_OPAQUE;
const uint DRAW_OFFSET_FLASHLIGHT  = DRAW_OFFSET_TRANSPARENT + DRAW_MAX_TRANSPARENT;
const uint DRAW_OFFSET_CSM0        = DRAW_OFFSET_FLASHLIGHT  + DRAW_MAX_FLASHLIGHT;
const uint DRAW_OFFSET_CSM1        = DRAW_OFFSET_CSM0        + DRAW_MAX_CSM0;
const uint DRAW_OFFSET_CSM2        = DRAW_OFFSET_CSM1        + DRAW_MAX_CSM1;
const uint DRAW_OFFSET_CSM3        = DRAW_OFFSET_CSM2        + DRAW_MAX_CSM2;

const uint DEBUG_MASK_OBB          = 1u << 0;
const uint DEBUG_MASK_SPHERE       = 1u << 1;
const uint DEBUG_MASK_AABB         = 1u << 2;

const uint DEBUG_VERTS_PER_OBB     = 24u;      // 12 edges x 2 verts, LINE_LIST
const uint DEBUG_VERTS_PER_SPHERE  = 72u;      // 3 rings x 12 segments x 2
const uint DEBUG_MAX_ITEMS         = 131072u;  // max debug items per frame
const uint DEBUG_MAX_VERTS         = DEBUG_MAX_ITEMS * DEBUG_VERTS_PER_OBB;

// final visible-instance buffer regions (what firstInstance indexes into)
// stream slot s owns [s * MAX_INSTANCES_PER_STREAM, ...)
uint streamInstanceBase(uint slot) { return slot * MAX_INSTANCES_PER_STREAM; }

// draw region base per stream
uint streamDrawBase(uint slot)
{
	if      (slot == VIS_SLOT_OPAQUE)      return DRAW_OFFSET_OPAQUE;
	else if (slot == VIS_SLOT_TRANSPARENT) return DRAW_OFFSET_TRANSPARENT;
	else if (slot == VIS_SLOT_CSM0)        return DRAW_OFFSET_CSM0;
	else if (slot == VIS_SLOT_CSM1)        return DRAW_OFFSET_CSM1;
	else if (slot == VIS_SLOT_CSM2)        return DRAW_OFFSET_CSM2;
	else if (slot == VIS_SLOT_CSM3)        return DRAW_OFFSET_CSM3;
	else                                   return DRAW_OFFSET_FLASHLIGHT;
}

struct BinKey        // static, uploaded at load — ABT_DrawBinKeys (global)
{
	uint meshID;     // BIN_INVALID = empty hash slot
	uint materialID;
	uint binID;
};

struct StreamEntry   // transient routing record — ABT_InstanceStreams
{
	uint visibleID;  // index into VisibleInstances
	uint binID;
};

struct IndirectIndexedDrawCmd
{
	uint indexCount;
	uint instanceCount;
	uint firstIndex;
	int vertexOffset;
	uint firstInstance;
};

struct IndirectDrawCmd
{
	uint vertexCount;
	uint instanceCount;
	uint firstVertex;
	uint firstInstance;
};

struct DrawBin
{
	uint meshID;
	uint materialID;

	uint instanceOffset;
	uint instanceCount;
};

struct DebugItem
{
	uint instanceID;
	uint drawType;
	uint colorPacked;
};

struct DebugVertex
{
	vec3 position;
	uint colorPacked;
};

struct DebugCounters
{
	uint itemCount;
	uint vertexCount;
};

uint packDebugColor(vec4 c)
{
	return (uint(c.a * 255.0) << 24) | (uint(c.b * 255.0) << 16)
		 | (uint(c.g * 255.0) << 8)  |  uint(c.r * 255.0);
}
vec4 unpackDebugColor(uint p)
{
	return vec4(float(p & 0xFFu), float((p >> 8) & 0xFFu),
				float((p >> 16) & 0xFFu), float((p >> 24) & 0xFFu)) / 255.0;
}

#endif
