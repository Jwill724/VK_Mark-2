#ifndef RT_CORE_GLSL
#define RT_CORE_GLSL

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_ray_query : require
#extension GL_KHR_shader_subgroup_ballot : require

#include "common.glsl"

const uint RT_INVALID_SLOT = 0xFFFFFFFFu;

const uint MAX_RT_INSTANCES = 32000u;

#define RT_MASK_SHADOW_CASTERS (RT_MASK_OPAQUE | RT_MASK_ALPHA_TESTED)

layout(set = FRAME_SET, binding = FRAME_BINDING_TLAS) uniform accelerationStructureEXT sceneTLAS;

struct RTInstance
{
	vec4  row0;
	vec4  row1;
	vec4  row2;
	uint  customIndexAndMask;
	uint  sbtOffsetAndFlags;
	uvec2 blasAddress;
};

layout(buffer_reference, scalar) readonly buffer RTRowsBuffer { uint rows[]; };
RTRowsBuffer getRTRowsBuffer() {
	return RTRowsBuffer(getABTGlobalAddress(ABT_RTRows));
}

layout(buffer_reference, scalar) buffer RTInstancesBuffer {
	RTInstance instances[];
};
RTInstancesBuffer getRTInstancesBuffer() {
	return RTInstancesBuffer(getABTFrameAddress(ABT_RTInstances));
}

layout(buffer_reference, scalar) buffer RTRayListBuffer {
	uint counts[RT_RAY_SLOT_COUNT];
	uint payload[];
};
RTRayListBuffer getRTRayListBuffer() {
	return RTRayListBuffer(getABTFrameAddress(ABT_RTRayList));
}

uint rtPackPixel(uvec2 px)  { return (px.y << 16) | px.x; }
uvec2 rtUnpackPixel(uint p) { return uvec2(p & 0xFFFFu, p >> 16); }

uint rtRayCount(RTRayListBuffer b, uint slot, uint capacity)
{
	return min(b.counts[slot], capacity);
}

uint rtRayOffset(uint slot, uint capacity) { return slot * capacity; }

uint rtCompactAppend(RTRayListBuffer b, uint slot, uint capacity, bool activeSlot)
{
	uvec4 ballot = subgroupBallot(activeSlot);
	uint  local  = subgroupBallotExclusiveBitCount(ballot);
	uint  total  = subgroupBallotBitCount(ballot);

	uint base = 0u;
	if (subgroupElect() && total > 0u)
		base = atomicAdd(b.counts[slot], total);
	base = subgroupBroadcastFirst(base);

	uint idx = base + local;
	return (activeSlot && idx < capacity) ? rtRayOffset(slot, capacity) + idx : RT_INVALID_SLOT;
}

uint rtBlasMeshID(InstanceInput inst)
{
	return meshFromLODIndex(inst, LOD_IDX_BASE);
}

#endif
