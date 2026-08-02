#ifndef INSTANCES_GLSL
#define INSTANCES_GLSL

#extension GL_GOOGLE_include_directive : require
#include "bindings.glsl"

// Stored in InstanceInput
const uint PASS_OPAQUE            = 1u << 0;
const uint PASS_TRANSPARENT       = 1u << 1;
const uint STATIC_OBJECT          = 1u << 2;
const uint DYNAMIC_OBJECT         = 1u << 3;
const uint CAST_CSM               = 1u << 4;
const uint CAST_FLASHLIGHT        = 1u << 5;
const uint RECEIVE_SHADOW         = 1u << 6;
const uint OCCLUDABLE             = 1u << 7;
const uint LOD_ENABLED            = 1u << 8;
const uint ALPHA_TESTED           = 1u << 9;
const uint DOUBLE_SIDED           = 1u << 10;
const uint GPU_SKINNED            = 1u << 11;
const uint ALWAYS_VISIBLE         = 1u << 12;
const uint IS_TREE                = 1u << 13;
const uint HAS_NORMALS            = 1u << 14;
const uint INSTANCE_ACTIVE        = 1u << 15;

// Stored in VisibleInstance
const uint VIS_PRIMARY_OPAQUE        = 1u << 0;
const uint VIS_PRIMARY_OPAQUE_MASKED = 1u << 1;
const uint VIS_PRIMARY_TRANSPARENT   = 1u << 2;
const uint VIS_FLASHLIGHT            = 1u << 3;
const uint VIS_CSM0                  = 1u << 4;
const uint VIS_CSM1                  = 1u << 5;
const uint VIS_CSM2                  = 1u << 6;
const uint VIS_CSM3                  = 1u << 7;

const uint VISIBILITY_TYPE_COUNT = 8u;

const uint LOD_IDX_LOD0    = 0u;
const uint LOD_IDX_LOD1    = 1u;
const uint LOD_IDX_LOD2    = 2u;
const uint LOD_IDX_LOD3    = 3u;
const uint LOD_IDX_BASE    = 4u;   // LOD_ENABLED off -> instance.meshID
const uint LOD_IDX_SHADOW0 = 5u;
const uint LOD_IDX_SHADOW1 = 6u;
const uint LOD_IDX_SHADOW2 = 7u;

const uint LOD_PACK_SHIFT   = 28u;
const uint LOD_PACK_PAYLOAD = (1u << LOD_PACK_SHIFT) - 1u;   // 0x0FFFFFFF
const uint LOD_PACK_MASK    = 7u;
const uint LOD_PACK_TRIP    = 1u << 31u;

uint visMeshletID(uint g)   { return g >> 8u; }
uint visLocalTri(uint g)    { return (g >> 1u) & 0x7Fu; }
bool visFrontFacing(uint g) { return (g & 1u) != 0u; }

uint packVisID(uint instanceID, uint lodIdx)
{
	return (instanceID & LOD_PACK_PAYLOAD)
		 | ((lodIdx & LOD_PACK_MASK) << LOD_PACK_SHIFT)
		 | LOD_PACK_TRIP;
}
uint visInstanceID(uint packed) { return packed & LOD_PACK_PAYLOAD; }
uint visLODIndex(uint packed)   { return (packed >> LOD_PACK_SHIFT) & LOD_PACK_MASK; }

const uint TRANSFORM_DYNAMIC_BIT = 1u << 31;
const uint TRANSFORM_INDEX_MASK  = ~TRANSFORM_DYNAMIC_BIT;

bool isDynamicTransform(uint transformID) { return (transformID & TRANSFORM_DYNAMIC_BIT) != 0u; }
uint transformIndex(uint transformID)     { return transformID & TRANSFORM_INDEX_MASK; }

struct InstanceInput
{
	uint meshID;
	uint materialID;
	uint transformID;
	uint meshletVisibilityOffset; 

	uint lod0;
	uint lod1;
	uint lod2;
	uint lod3;

	uint shadowLod0;
	uint shadowLod1;
	uint shadowLod2;

	uint flags;
};

struct VisibleInstance
{
	uint instanceID;
	uint lodIndex;
	uint selectedMesh;
	uint visMask;
};

struct InstanceCursors
{
	uint cursors[VIS_SLOT_COUNT];
};

uint meshFromLODIndex(InstanceInput inst, uint idx)
{
	if (idx == LOD_IDX_LOD0)    return inst.lod0;
	if (idx == LOD_IDX_LOD1)    return inst.lod1;
	if (idx == LOD_IDX_LOD2)    return inst.lod2;
	if (idx == LOD_IDX_LOD3)    return inst.lod3;
	if (idx == LOD_IDX_BASE)    return inst.meshID;
	if (idx == LOD_IDX_SHADOW0) return inst.shadowLod0;
	if (idx == LOD_IDX_SHADOW1) return inst.shadowLod1;
	return inst.shadowLod2;
}

uint packStreamBin(uint binID, uint lodIdx)
{
	return (binID & LOD_PACK_PAYLOAD) | ((lodIdx & LOD_PACK_MASK) << LOD_PACK_SHIFT);
}
uint streamBinID(uint packed)    { return packed & LOD_PACK_PAYLOAD; }
uint streamLODIndex(uint packed) { return (packed >> LOD_PACK_SHIFT) & LOD_PACK_MASK; }

uint selectLODIndex(InstanceInput instance, vec3 center, float radius, vec3 camPos, float projScaleY)
{
	if ((instance.flags & LOD_ENABLED) == 0u) return LOD_IDX_BASE;

	float dist = max(length(center - camPos) - radius, 0.0);
	float screenRadius = (dist > 0.0) ? (radius * projScaleY) / dist : 1.0;

	if      (screenRadius < 0.02) return LOD_IDX_LOD3;
	else if (screenRadius < 0.05) return LOD_IDX_LOD2;
	else if (screenRadius < 0.10) return LOD_IDX_LOD1;
	else                          return LOD_IDX_LOD0;
}

uint selectShadowLODIndex(InstanceInput instance, uint cascadeIndex)
{
	uint slot;
	if      (cascadeIndex == 0u) slot = 0u;
	else if (cascadeIndex == 2u) slot = 1u;
	else if (cascadeIndex == 1u) slot = 0u;
	else                         slot = 2u;

	bool isAlphaTested = (instance.flags & ALPHA_TESTED) != 0u;
	bool isTree        = (instance.flags & IS_TREE) != 0u;
	if (isAlphaTested && isTree && slot > 0u)
		slot -= 1u;

	return LOD_IDX_SHADOW0 + slot;
}

uint selectLOD(InstanceInput instance, vec3 center, float radius, vec3 camPos, float projScaleY)
{
	return meshFromLODIndex(instance, selectLODIndex(instance, center, radius, camPos, projScaleY));
}

uint selectShadowLOD(InstanceInput instance, uint cascadeIndex)
{
	return meshFromLODIndex(instance, selectShadowLODIndex(instance, cascadeIndex));
}

#endif
