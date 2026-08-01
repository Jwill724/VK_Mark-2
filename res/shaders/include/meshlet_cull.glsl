#ifndef MESHLET_CULL_GLSL
#define MESHLET_CULL_GLSL

#extension GL_GOOGLE_include_directive : require
#include "common.glsl"
#include "culling.glsl"

const uint MAX_MESHLET_VISIBILITY_BITS = 33554432u;

struct MeshletPayload
{
	uint packedID;
	uint vertexOffset;
	uint meshletIndices[TASK_GROUP_SIZE];
};

vec3 decodeConeAxis(Meshlet ml, mat3 nrmMat)
{
	vec3 a = vec3(snorm8ToFloat(int(ml.coneAxis.x)),
				  snorm8ToFloat(int(ml.coneAxis.y)),
				  snorm8ToFloat(int(ml.coneAxis.z)));
	return normalize(nrmMat * a);
}

// flip = +1.0 for back-face-cull passes
// flip = -1.0 for front-face-cull passes
bool coneCullPerspective(
	vec3 centerWS, float radiusWS, vec3 axisWS, float cutoff, vec3 eyeWS, float flip)
{
	vec3 d = centerWS - eyeWS;
	return dot(d, axisWS * flip) >= cutoff * length(d) + radiusWS;
}

// orthographic / directional: all rays parallel, no radius term needed
bool coneCullDirectional(vec3 axisWS, float cutoff, vec3 lightDir, float flip)
{
	return dot(axisWS * flip, lightDir) >= cutoff;
}

// eye.w == 0 -> eye.xyz is a direction (directional), else a position
bool meshletConeCulled(
	Meshlet ml, mat4 model, vec3 centerWS, float radiusWS, vec4 eye, float flip)
{
	vec3  axisWS = decodeConeAxis(ml, mat3(model));
	float cutoff = snorm8ToFloat(int(ml.coneCutoff));

	return (eye.w == 0.0)
		? coneCullDirectional(axisWS, cutoff, eye.xyz, flip)
		: coneCullPerspective(centerWS, radiusWS, axisWS, cutoff, eye.xyz, flip);
}

bool meshletVisibleLastFrame(uint bit)
{
	if (bit >= MAX_MESHLET_VISIBILITY_BITS) return false;

	uint word = getMeshletVisibilityABuffer().bits[bit >> 5u];
	return (word & (1u << (bit & 31u))) != 0u;
}

#endif
