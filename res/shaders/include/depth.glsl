#ifndef DEPTH_GLSL
#define DEPTH_GLSL

const uint HI_Z_MIP_COUNT = 5u;

// Depth conventions in renderer are ZERO_TO_ONE and RIGHT_HANDED

struct ViewReconstructResult
{
	vec3 pos;
	float viewDepth;
};

float sampleHiZDepth(sampler2D hiZ, vec2 uv, float radiusHalfRes)
{
	// convert to approximate full-res footprint
	float fullResRadius = max(radiusHalfRes * 2.0, 1.0);

	// mip level ~ log2(footprint)
	float mipLevel = log2(fullResRadius);

	mipLevel = clamp(mipLevel, 0.0, float(HI_Z_MIP_COUNT));
	return textureLod(hiZ, uv, mipLevel).r;
}

float linearizeDepth(float depth, float nearPlane, float farPlane) {
	return (nearPlane * farPlane) / (farPlane - depth * (farPlane - nearPlane));
}

// Volumetric light needs depth at [-1, 1] to be consistent
ViewReconstructResult reconstructWorldPosFromDepth(vec2 uv, float depthValue, mat4 invProj, mat4 invView)
{
	vec4 ndc;
	ndc.x = uv.x * 2.0 - 1.0;
	ndc.y = (1.0 - uv.y) * 2.0 - 1.0; // Inverts projection
	ndc.z = depthValue * 2.0 - 1.0;
	ndc.w = 1.0;

	vec4 viewPos = invProj * ndc;
	viewPos /= viewPos.w;

	vec4 worldPos = invView * vec4(viewPos.xyz, 1.0);

	ViewReconstructResult result;
	result.pos = worldPos.xyz;
	result.viewDepth = -viewPos.z;

	return result;
}

ViewReconstructResult reconstructViewSpaceFromDepth(vec2 uv, float depthValue, mat4 invProj)
{
	vec4 ndc;
	ndc.x = uv.x * 2.0 - 1.0;
	ndc.y = (1.0 - uv.y) * 2.0 - 1.0; // Inverts projection
	ndc.z = depthValue;
	ndc.w = 1.0;

	vec4 viewPos = invProj * ndc;
	viewPos /= viewPos.w;

	ViewReconstructResult result;
	result.pos = viewPos.xyz;
	result.viewDepth = -viewPos.z;

	return result;
}

#endif