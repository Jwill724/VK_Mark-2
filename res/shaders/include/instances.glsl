#ifndef INSTANCES_GLSL
#define INSTANCES_GLSL

#extension GL_GOOGLE_include_directive : require
#include "bindings.glsl"

// Stored in InstanceInput
const uint PASS_OPAQUE            = 1u << 0;
const uint PASS_TRANSPARENT       = 1u << 1;
const uint STATIC_OBJECT          = 1u << 2;
const uint DYNAMIC_OBJECT         = 1u << 3;
const uint CAST_FLASHLIGHT        = 1u << 5;
const uint RECEIVE_SHADOW         = 1u << 6;
const uint OCCLUDABLE             = 1u << 7;
const uint LOD_ENABLED            = 1u << 8;
const uint ALPHA_TESTED           = 1u << 9;
const uint CAST_CSM               = 1u << 4;
const uint GPU_SKINNED            = 1u << 10;
const uint ALWAYS_VISIBLE         = 1u << 11;
const uint IS_TREE                = 1u << 12;
const uint HAS_NORMALS            = 1u << 13;
const uint INSTANCE_ACTIVE        = 1u << 14;

// Stored in VisibleInstance
const uint VIS_PRIMARY_OPAQUE      = 1u << 0;
const uint VIS_PRIMARY_TRANSPARENT = 1u << 1;
const uint VIS_FLASHLIGHT          = 1u << 2;
const uint VIS_CSM0                = 1u << 3;
const uint VIS_CSM1                = 1u << 4;
const uint VIS_CSM2                = 1u << 5;
const uint VIS_CSM3                = 1u << 6;

struct InstanceInput
{
	uint meshID;
	uint materialID;
	uint transformID;

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
	uint selectedMesh;
	uint passFlags;
};

struct InstanceCursors
{
	uint cursors[VIS_SLOT_COUNT];
};

// Define baseline lod at visibility
uint selectLOD(InstanceInput instance, vec3 center, float radius, vec3 camPos, float projScaleY)
{
	if ((instance.flags & LOD_ENABLED) == 0u) return instance.meshID;

	float dist = max(length(center - camPos) - radius, 0.0);

	float screenRadius = (dist > 0.0) ? (radius * projScaleY) / dist : 1.0;

	if      (screenRadius < 0.02) return instance.lod3;
	else if (screenRadius < 0.05) return instance.lod2;
	else if (screenRadius < 0.10) return instance.lod1;
	else                          return instance.lod0;
}

// If viable occluder, than adjust for pass flag vis bitmask slot
uint selectShadowLOD(InstanceInput instance, uint cascadeIndex)
{
	uint slot;
	if      (cascadeIndex == 0u) slot = 0u;
	else if (cascadeIndex == 2u) slot = 1u;
	else if (cascadeIndex == 1u) slot = 0u;
	else                         slot = 2u;

	// foliage bias — alpha tested trees get one slot better on far cascades
	bool isAlphaTested = (instance.flags & ALPHA_TESTED) != 0u;
	bool isTree        = (instance.flags & IS_TREE) != 0u;
	if (isAlphaTested && isTree && slot > 0u)
		slot -= 1u;

	if      (slot == 0u) return instance.shadowLod0;
	else if (slot == 1u) return instance.shadowLod1;
	else                 return instance.shadowLod2;
}

#endif
