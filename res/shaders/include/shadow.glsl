#ifndef SHADOW_GLSL
#define SHADOW_GLSL

const float flashlightShadowTexel = 1.0 / 512.0;

const float shadowFar = 1000.0;

const uint SHADOW_FILTER_PCF  = 0u;
//const uint SHADOW_FILTER_PCSS = 1u;

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

mat2 createHash(vec2 pixelCoord)
{
	float ang = interleavedGradientNoise(pixelCoord) * 6.2831853;
	return mat2(cos(ang), -sin(ang), sin(ang), cos(ang));
}

mat2 createHashTemporal(vec2 pixelCoord, uint frameIndex)
{
	vec2 jitteredPixel = pixelCoord + float(frameIndex) * vec2(1.618033988, 1.324717957);
	float ang = interleavedGradientNoise(jitteredPixel) * 6.2831853;
	return mat2(cos(ang), -sin(ang), sin(ang), cos(ang));
}

float gaussianWeight(vec2 diskPos)
{
	float d2 = dot(diskPos, diskPos); // d in [0, 1] on unit disk, so d2 in [0,1]
	return exp(-d2 * 2.0);
}

// Used for volumetric lights shadow map samples
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

// Used in primary shadow rendering
float PCFPoissonHigh(
	mat2  poissonRotation,
	uint  shadowMapID,
	vec2  shadowUV,
	float receiverDepth,
	float bias,
	float texel,
	float radius)
{
	float samplePos = texel * radius;   // UV-space kernel radius
	float depthPos  = receiverDepth + bias;

	float sum       = 0.0;
	float weightSum = 0.0;

	for (int i = 0; i < 16; ++i) {
		vec2  diskPos    = poisson16[i];
		vec2  offset     = (poissonRotation * diskPos) * samplePos;

		float depthSample = SampleTexture(shadowMapID, shadowUV + offset).r;
		float shadow      = depthSample >= depthPos ? 1.0 : 0.0;

		// Gaussian weight keyed to unit-disk distance
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

#endif
