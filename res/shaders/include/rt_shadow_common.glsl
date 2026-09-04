#ifndef RTSHADOW_COMMON_GLSL
#define RTSHADOW_COMMON_GLSL

#extension GL_GOOGLE_include_directive : require

#include "rt_params.glsl"
#include "depth.glsl"

layout(push_constant) uniform RTShadowPush {
	vec2 resolution;
	vec2 invResolution;

	RTShadowParams shadow;

	uint rayBase;
	uint rayCapacity;
	uint hilbertLutID;

	float saturationEps;
	float disocclusionScale;
	uint  pad0[3];
} sp;

#endif
