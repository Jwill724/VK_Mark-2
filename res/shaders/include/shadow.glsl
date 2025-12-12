#ifndef SHADOW_GLSL
#define SHADOW_GLSL

const uint MAX_CASCADES = 4u;

struct ShadowCSM {
	mat4 cascadeVP[MAX_CASCADES];
	vec4 cascadeSplits;
	vec4 params; // .x/shadowBias, .y/shadowMapID, .z/cascadeCount, .w/texelSize
	vec4 cascadeRadii;
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

mat2 createHash(vec2 pixelCoord)
{
	float h = fract(sin(dot(pixelCoord, vec2(12.9898, 78.233))) * 43758.5453);
	float ang = h * 6.2831853;
	mat2 R = mat2(cos(ang), -sin(ang), sin(ang), cos(ang));
	return R;
}

// Used for volumetric lights shadow map samples
float PCFPoissonLow(
	vec2 pixelCoord,
	sampler2DArray sm,
	vec2 uv,
	uint layer,
	float z,
	float bias,
	float texel)
{
	mat2 R = createHash(pixelCoord);
	float s = 0.0;
	float depthPos = z - bias;
	for (int i = 0; i < 8; ++i) {
		vec2 offset = (R * poisson8[i]) * texel;
		float depthSample = texture(sm, vec3(uv + offset, float(layer))).r;
		s += float((depthPos) < depthSample);
	}

	return s * (1.0 / 8.0);
}

// Used in primary shadow rendering
float PCFPoissonHigh(
	vec2 pixelCoord,
	sampler2DArray sm,
	vec2 uv,
	uint layer,
	float z,
	float bias,
	float texel,
	float radius)
{
	mat2 R = createHash(pixelCoord);
	float sum = 0.0;
	float samplePos = texel * radius;
	float depthPos = z - bias * texel;

	for (int i = 0; i < 16; ++i) {
		vec2 offset = (R * poisson16[i]) * samplePos;

		float depthSample = texture(sm, vec3(uv + offset, float(layer))).r;
		float shadow = depthSample >= depthPos ? 1.0 : 0.0;

		float w = 1.0 - smoothstep(0.0, radius, length(offset));
		sum += shadow * w;
	}

	return sum / 16.0;
}

// Right handed view looks down -Z
uint cascadeViewDepthSplit(float viewDepth, uint cascadeCount, vec4 cascadeSplits) {
	const uint maxCascade = cascadeCount - 1u;

	uint cascadeIdx = maxCascade;
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