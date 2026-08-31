#ifndef DEPTH_GLSL
#define DEPTH_GLSL

// Depth conventions in renderer are ZERO_TO_ONE and RIGHT_HANDED
// Reversed Z depth is standard
#define DEPTH_EPSILON_REVERSED_Z 1e-6

// Saw this and was like hell yeah
// https://x.com/Cody_J_Bennett/status/2025156053323690276/photo/4

// About the min max uint packed format for hi z
// https://martinfullerblog.wordpress.com/2023/01/13/min-max-buffer-precision-improvement/
const uint MAX_BITS   = 17u;
const uint RATIO_BITS = 15u;
const uint MAX_MASK   = (1u << MAX_BITS) - 1u;
const uint RATIO_MASK = (1u << RATIO_BITS) - 1u;

uint packHZB(float minDepth01, float maxDepth01)
{
	float safeMax = max(maxDepth01, 0.0);
	float ratio = (safeMax > 0.0) ? (minDepth01 / safeMax) : 1.0;

	uint uMax   = min(uint(ceil(clamp(maxDepth01, 0.0, 1.0) * float(MAX_MASK))), MAX_MASK);
	uint uRatio = min(uint(ceil(clamp(ratio, 0.0, 1.0) * float(RATIO_MASK))), RATIO_MASK);

	return (uMax << RATIO_BITS) | (uRatio & RATIO_MASK);
}

void unpackHZB(uint packed, out float minDepth01, out float maxDepth01)
{
	maxDepth01 = float(packed >> RATIO_BITS) / float(MAX_MASK);
	float ratio = float(packed & RATIO_MASK) / float(RATIO_MASK);
	minDepth01 = ratio * maxDepth01;
}

float unpackNearRaw(uint packed)
{
	return float(packed >> RATIO_BITS) / float(MAX_MASK);
}

float linearizeDepth(float depth, float nearPlane, float farPlane) {
	return (nearPlane * farPlane) / (nearPlane + depth * (farPlane - nearPlane));
}

float screenToViewDepth(float screenDepth, float depthLinearizeMult, float depthLinearizeAdd)
{
	// Optimization by XeGTAO
	// https://github.com/GameTechDev/XeGTAO/blob/a5b1686c7ea37788eeb3576b5be47f7c03db532c/Source/Rendering/Shaders/XeGTAO.hlsli#L112
	return depthLinearizeMult / (depthLinearizeAdd - screenDepth);
}

float sampleHiZMinDepth(usampler2D hiZ, vec2 uv, float radiusHalfRes, float nearPlane, float farPlane)
{
	// convert to approximate full-res footprint
	float fullResRadius = max(radiusHalfRes * 2.0, 1.0);

	// mip level ~ log2(footprint)
	float mipLevel = log2(fullResRadius);
	float maxMip = float(textureQueryLevels(hiZ) - 1);
	mipLevel = clamp(mipLevel, 0.0, maxMip);

	uvec4 sampleDepth = textureLod(hiZ, uv, mipLevel);
	float nearRaw = unpackNearRaw(sampleDepth.r);
	float nearLinear = linearizeDepth(nearRaw, nearPlane, farPlane);

	return nearLinear;
}

struct ViewReconstructResult
{
	vec3 pos;
	float viewDepth;
};

ViewReconstructResult reconstructWorldPosFromDepth(vec2 uv, float depthValue, mat4 invProj, mat4 invView)
{
	vec4 ndc;
	ndc.x = uv.x * 2.0 - 1.0;
	ndc.y = (1.0 - uv.y) * 2.0 - 1.0; // Inverts projection
	ndc.z = depthValue;
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

vec3 cheapReconstructViewPos(vec2 uv, float depthVS, vec2 ndcToViewMul, vec2 ndcToViewAdd) {
	vec3 pos;
	pos.xy = (ndcToViewMul * uv + ndcToViewAdd) * depthVS;
	pos.z = -depthVS;
	return pos;
}

// xy = uv, z = 1/viewDepth (screen-space linear, safe to lerp)
vec3 viewToRayScreen(vec3 posV, mat4 proj)
{
	vec4 c = proj * vec4(posV, 1.0);
	return vec3(c.xy / c.w * vec2(0.5, -0.5) + 0.5, 1.0 / (-posV.z));
}

vec3 viewToUVDepth(vec3 posV, mat4 proj)
{
	vec4 c = proj * vec4(posV, 1.0);
	vec3 ndc = c.xyz / c.w;
	return vec3(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5, ndc.z);
}

#endif
