#ifndef SHADOW_GLSL
#define SHADOW_GLSL

#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

const float FLASHLIGHT_TEXEL_SIZE = 1.0 / 512.0;

// The only number that works
const float MIN_SHADOW_BIAS = 0.0001;

const float SHADOW_BASE_BIAS_TEXELS  = 0.5;
const float SHADOW_SLOPE_BIAS_TEXELS = 0.8;

// 8-tap Poisson disk in texels
const vec2 poisson8[8] = vec2[](
  vec2( 0.0, -0.5), vec2( 0.5,  0.0), vec2( 0.0,  0.5), vec2(-0.5,  0.0),
  vec2( 0.35, 0.35), vec2(-0.35, 0.35), vec2( 0.35,-0.35), vec2(-0.35,-0.35)
);

const vec2 poisson16[16] = vec2[](
	vec2(-0.94201624, -0.39906216),
	vec2( 0.94558609, -0.76890725),
	vec2(-0.094184101, -0.92938870),
	vec2( 0.34495938,  0.29387760),
	vec2(-0.91588581,  0.45771432),
	vec2(-0.81544232, -0.87912464),
	vec2(-0.38277543,  0.27676845),
	vec2( 0.97484398,  0.75648379),
	vec2( 0.44323325, -0.97511554),
	vec2( 0.53742981, -0.47373420),
	vec2(-0.26496911, -0.41893023),
	vec2( 0.79197514,  0.19090188),
	vec2(-0.24188840,  0.99706507),
	vec2(-0.81409955,  0.91437590),
	vec2( 0.19984126, -0.78641367),
	vec2( 0.14383161, -0.14100790)
);

float gaussianWeight(vec2 diskPos)
{
	float d2 = dot(diskPos, diskPos); // d in [0, 1] on unit disk, so d2 in [0,1]
	return exp(-d2 * 2.0);
}

// Pushes the receiver along its normal to escape self-shadowing acne.
// Offset grows with grazing angle (slope = sin of the angle to the light)
// and is scaled into world units by the cascade's texel size.
vec3 computeNormalOffset(vec3 normalWS, vec3 L, float texelSizeWorld)
{
	float nDotL = dot(normalWS, L);
	float slope = sqrt(clamp(1.0 - nDotL * nDotL, 0.0, 1.0));
	float offsetAmount = (SHADOW_BASE_BIAS_TEXELS + slope * SHADOW_SLOPE_BIAS_TEXELS) * texelSizeWorld;
	return normalWS * offsetAmount;
}

// NDC depth change per world unit along the cascade's depth axis.
// Ortho => w==1, so row-2 of the linear part is the gradient; its length
// is 1/depthRange. Correct per-cascade even under tight/SDSM fitting.
float ndcDepthPerWorld(mat4 cascadeVP)
{
	return length(vec3(cascadeVP[0][2], cascadeVP[1][2], cascadeVP[2][2]));
}

// Max legitimate per-tap depth correction (NDC) for receiver-plane bias:
// the depth span across one kernel radius for the steepest slope we trust.
float computeMaxPlaneBias(float radiusTexels, float worldPerTexel, float ndcPerWorld)
{
	float kernelWorldRadius = radiusTexels * worldPerTexel; // texels -> world
	return kernelWorldRadius * ndcPerWorld;                 // world -> NDC
}

// Used for volumetric directional and flashlight
float PCFPoissonLow(
	mat2  poissonRotation,
	uint  shadowMapID,
	vec2  shadowUV,
	float receiverDepth,
	float bias,
	float texel)
{
	float sum      = 0.0;
	float depthPos = receiverDepth + bias;
	for (int i = 0; i < 8; ++i) {
		vec2 offset = (poissonRotation * poisson8[i]) * texel;
		float depthSample = SampleTexture(shadowMapID, shadowUV + offset).r;
		sum              += float(depthPos < depthSample);
	}

	return sum * (1.0 / 8.0);
}

// Remaining PCF functions for directional shadow map

float PCFPoissonHigh(
	mat2  poissonRotation,
	uint  shadowMapID,
	vec2  shadowUV,
	float receiverDepth,
	float texel,
	float radius,
	vec2 atlasMin,
	vec2 atlasMax,
	vec2 depthGradient,
	float maxPlaneBias)
{
	float samplePos = texel * radius;

	float sum       = 0.0;
	float weightSum = 0.0;

	for (int i = 0; i < 16; ++i) {
		vec2  diskPos    = poisson16[i];
		vec2  offset     = (poissonRotation * diskPos) * samplePos;

		vec2 sampleUV = clamp(shadowUV + offset, atlasMin, atlasMax);
		vec2 actualOffset = sampleUV - shadowUV; 
		float planeBias = clamp(dot(actualOffset, depthGradient), -maxPlaneBias, maxPlaneBias);
		float depthPos  = receiverDepth + planeBias + MIN_SHADOW_BIAS;

		float depthSample = SampleTexture(shadowMapID, sampleUV).r;
		float shadow      = depthSample >= depthPos ? 1.0 : 0.0;

		float w    = gaussianWeight(diskPos);
		sum       += shadow * w;
		weightSum += w;
	}

	return weightSum > 1e-6 ? sum / weightSum : 1.0;
}

vec2 vogelSample(int i, int count, float phi) {
	float r     = sqrt(float(i) + 0.5) / sqrt(float(count));
	float theta = float(i) * 2.4 + phi; // 2.4 = golden angle in radians
	return vec2(r * cos(theta), r * sin(theta));
}

float PCFVogel(
	mat2  poissonRotation,
	uint  shadowMapID,
	vec2  shadowUV,
	float receiverDepth,
	float texel,
	float radius,
	vec2 atlasMin,
	vec2 atlasMax,
	vec2 depthGradient,
	float maxPlaneBias)
{
	float samplePos = texel * radius;

	float phi = atan(poissonRotation[0][1], poissonRotation[0][0]);

	float sum       = 0.0;
	float weightSum = 0.0;

	for (int i = 0; i < 16; ++i) {
		vec2  diskPos = vogelSample(i, 16, phi);
		vec2  offset  = diskPos * samplePos;

		vec2 sampleUV = clamp(shadowUV + offset, atlasMin, atlasMax);
		vec2 actualOffset = sampleUV - shadowUV; 

		float planeBias = clamp(dot(actualOffset, depthGradient), -maxPlaneBias, maxPlaneBias);
		float depthPos  = receiverDepth + planeBias + MIN_SHADOW_BIAS;

		float depthSample = SampleTexture(shadowMapID, sampleUV).r;
		float shadow      = depthSample >= depthPos ? 1.0 : 0.0;

		float w    = gaussianWeight(diskPos);
		sum       += shadow * w;
		weightSum += w;
	}

	return weightSum > 1e-6 ? sum / weightSum : 1.0;
}

// Right handed view looks down -Z
uint cascadeViewDepthSplit(float viewDepth, uint cascadeCount, vec4 cascadeSplits)
{
	uint cascadeIdx = cascadeCount - 1u;
	for (uint i = 0u; i < cascadeCount; ++i) {
		if (viewDepth < cascadeSplits[i]) {
			cascadeIdx = i;
			break;
		}
	}

	return cascadeIdx;
}

vec3 cascadeColor(uint i)
{
	const vec3 C[4] = vec3[](
		vec3(1, 0, 0),
		vec3(0, 1, 0),
		vec3(0, 0, 1),
		vec3(1, 1, 0)
	);
	return C[min(i, 3u)];
}

bool casterOverlapsReceivers(
	vec3 casterCenterWS, vec3 casterExtentWS,
	mat4 lightView,
	vec3 receiverLSMin, vec3 receiverLSMax,
	float lsEpsilon)
{
	mat3 rot = mat3(lightView);
	mat3 absRot = mat3(abs(rot[0]), abs(rot[1]), abs(rot[2]));

	vec3 centerLS = (lightView * vec4(casterCenterWS, 1.0)).xyz;
	vec3 extentLS = absRot * casterExtentWS;

	vec3 cMin = centerLS - extentLS;
	vec3 cMax = centerLS + extentLS;

	// X/Y: shadow projects down light-space Z, need footprint overlap
	if (cMin.x > receiverLSMax.x + lsEpsilon || cMax.x < receiverLSMin.x - lsEpsilon) return false;
	if (cMin.y > receiverLSMax.y + lsEpsilon || cMax.y < receiverLSMin.y - lsEpsilon) return false;

	// Z: keep everything toward the light, reject only fully behind receivers
	if (cMax.z < receiverLSMin.z - lsEpsilon) return false;

	return true;
}

#endif
