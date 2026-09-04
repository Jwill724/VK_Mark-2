#ifndef REFLECT_COMMON_GLSL
#define REFLECT_COMMON_GLSL

#extension GL_GOOGLE_include_directive : require

#include "depth.glsl"
#include "rt_shade.glsl"

layout(push_constant) uniform ReflectPush
{
	vec2 halfResSize;
	vec2 halfResTexel;

	RTShadowParams shadow;

	float reflectRoughnessCutoff;
	float roughnessFadeStart;
	float ambientScale;
	float bounceRoughnessCutoff;

	uint  noiseIndex;
	uint  hilbertLutID;
	uint  skyboxID;
	uint  brdfID;

	uint  specularID;
	uint  maxBounces;
	uint  maxReflectLights;
	uint  rayCapacity;

	uint rayBase;
	float shadowSkipThreshold;
	uint pad0[2];
} rp;

RTShadeParams rtReflectParamsFromPush()
{
	RTShadeParams p;
	p.shadow       = rp.shadow;
	p.ambientScale = rp.ambientScale;
	p.skyboxID     = rp.skyboxID;
	p.brdfID       = rp.brdfID;
	p.specularID   = rp.specularID;
	p.maxLights    = rp.maxReflectLights;
	p.shadowSkipThreshold = rp.shadowSkipThreshold;
	return p;
}

vec3 reflectFallbackIBL(vec3 N, vec3 V, float roughness)
{
	vec3 R = reflect(-V, N);
	int levels = SampleCubeQueryLevels(rp.specularID);
	float lod = clamp(roughness * float(levels - 1), 0.0, float(levels - 1));
	return SampleCubeLod(rp.specularID, vec3(R.x, -R.y, R.z), lod).rgb;
}

#endif
