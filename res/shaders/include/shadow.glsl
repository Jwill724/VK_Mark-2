#ifndef SHADOW_GLSL
#define SHADOW_GLSL

#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

const float FLASHLIGHT_TEXEL_SIZE = 1.0 / 512.0;

const float CASCADE_BLEND_FRACTION = 0.9;
const float CASCADE_LAST_FADE_FRACTION  = 0.98;

// The only number that works
const float MIN_SHADOW_BIAS = 0.0001;

const float SHADOW_BASE_BIAS_TEXELS  = 0.5;
const float SHADOW_SLOPE_BIAS_TEXELS = 0.8;

struct CascadeProj
{
	vec2  atlasUV;
	vec2  atlasMin;
	vec2  atlasMax;
	float depth;
	vec2  depthGrad;
	float maxPlaneBias;
	float radius;
	bool  valid;
};

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

float sampleCascade(CascadeProj p, mat2 hash, uint shadowMapID, float texel)
{
	return PCFVogel(
		hash, shadowMapID, p.atlasUV, p.depth,
		texel, p.radius, p.atlasMin, p.atlasMax, p.depthGrad, p.maxPlaneBias);
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

bool cascadeIsActive(uvec4 activeC, uint c)
{
	uint flag = (c == 0u) ? activeC.x
			  : (c == 1u) ? activeC.y
			  : (c == 2u) ? activeC.z
						  : activeC.w;
	return flag != 0u;
}

// Receiver-plane depth gradient. Ortho cascades are affine, so the
// inverse-transpose maps the world normal straight to clip space, where the plane
// gives dz/dxy = -n.xy / n.z exactly.
vec2 analyticDepthGradient(vec3 nWS, mat4 invTransVP, vec4 atlas)
{
	vec3 nClip = mat3(invTransVP) * nWS;

	if (abs(nClip.z) < 1e-6) return vec2(0.0);

	float dzdx = -nClip.x / nClip.z;
	float dzdy = -nClip.y / nClip.z;

	return vec2(dzdx * ( 2.0 / atlas.x),
				dzdy * (-2.0 / atlas.y));
}

void projectToCascade(vec3 worldPos, mat4 cascadeVP, vec4 atlas, out vec2 atlasUV, out float depth01)
{
	vec4 lsPos      = cascadeVP * vec4(worldPos, 1.0);
	vec3 projCoords = lsPos.xyz / lsPos.w;
	vec2 uv         = projCoords.xy * 0.5 + 0.5;
	uv.y            = 1.0 - uv.y;
	atlasUV         = uv * atlas.xy + atlas.zw;
	depth01         = projCoords.z;
}

// Builds the projection for cascade `c` and reports whether it can actually be
// sampled: the tile must have been rendered this frame (active), and the sample
// point must sit inside the tile with enough margin for the filter footprint.
CascadeProj buildCascadeProj(
	ShadowCSM csm, uvec4 activeC, uint c,
	vec3 worldPos, vec3 geometricNormalWS, vec3 L, float texel)
{
	CascadeProj o;
	o.valid = false;

	if (!cascadeIsActive(activeC, c)) return o;

	const vec4  atlas      = csm.atlasUV[c];
	const mat4  vp         = csm.cascadeVP[c];
	const float worldTexel = csm.cascadeWorldTexels[c];

	o.radius       = csm.maxFilterRadiusTexels[c];
	o.atlasMin     = atlas.zw;
	o.atlasMax     = atlas.zw + atlas.xy;
	o.maxPlaneBias = computeMaxPlaneBias(o.radius, worldTexel, ndcDepthPerWorld(vp));
	o.depthGrad    = analyticDepthGradient(geometricNormalWS, csm.cascadeInvTransVP[c], atlas);

	vec3 offsetPos = worldPos + computeNormalOffset(geometricNormalWS, L, worldTexel * o.radius);
	projectToCascade(offsetPos, vp, atlas, o.atlasUV, o.depth);

	vec2 shadowUV = (o.atlasUV - atlas.zw) / atlas.xy;

	// Filter footprint in tile-normalized units
	vec2 margin = (texel * o.radius) / atlas.xy;

	o.valid = all(greaterThanEqual(shadowUV, margin))
		   && all(lessThanEqual(shadowUV, vec2(1.0) - margin))
		   && o.depth >= 0.0 && o.depth <= 1.0;

	return o;
}

#endif
