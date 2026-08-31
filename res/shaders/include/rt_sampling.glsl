#ifndef RT_SAMPLING_GLSL
#define RT_SAMPLING_GLSL

#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

const float RT_SUN_ANGULAR_RADIUS = 0.00465;

const ivec2 RT_NOISE_OFFSET_SHADOW     = ivec2(0, 0);
const ivec2 RT_NOISE_OFFSET_REFLECTION = ivec2(23, 41);

vec2 rtNoise(ivec2 px, uint hilbertLutID, uint frameNumber, ivec2 passOffset)
{
	return SpatioTemporalNoise(px + passOffset, hilbertLutID, frameNumber % 64u);
}

mat3 buildTBN(vec3 n)
{
	float s = n.z >= 0.0 ? 1.0 : -1.0;
	float a = -1.0 / (s + n.z);
	float b = n.x * n.y * a;
	return mat3(vec3(1.0 + s * n.x * n.x * a, s * b, -s * n.x),
				vec3(b, s + n.y * n.y * a, -n.y),
				n);
}

vec2 rtVogelDisk(int i, int count, float phi, float jitter)
{
	float r     = sqrt(float(i) + jitter) / sqrt(float(count));
	float theta = float(i) * 2.4 + phi;
	return vec2(r * cos(theta), r * sin(theta));
}

vec3 rtConeDirection(mat3 basis, vec2 disk, float tanRadius)
{
	return normalize(basis[2] + (basis[0] * disk.x + basis[1] * disk.y) * tanRadius);
}

float rtSunTanRadius(float softness)
{
	return tan(RT_SUN_ANGULAR_RADIUS * softness);
}

float rtSphereTanRadius(float lightRadius, float distToLight)
{
	return lightRadius / max(distToLight, 1e-4);
}

vec3 sampleGGXVNDF(vec3 Ve, float alpha, vec2 u)
{
	vec3 Vh = normalize(vec3(alpha * Ve.x, alpha * Ve.y, Ve.z));
	float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
	vec3 T1 = lensq > 0.0 ? vec3(-Vh.y, Vh.x, 0.0) * inversesqrt(lensq) : vec3(1.0, 0.0, 0.0);
	vec3 T2 = cross(Vh, T1);
	float r = sqrt(u.x);
	float phi = TWO_PI * u.y;
	float t1 = r * cos(phi);
	float t2 = r * sin(phi);
	float s = 0.5 * (1.0 + Vh.z);
	t2 = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;
	vec3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;
	return normalize(vec3(alpha * Nh.x, alpha * Nh.y, max(0.0, Nh.z)));
}

float ggxD(float NoH, float alpha)
{
	float a2 = alpha * alpha;
	float d = NoH * NoH * (a2 - 1.0) + 1.0;
	return a2 / max(PI * d * d, 1e-7);
}

float smithG1(float NoV, float alpha)
{
	float a2 = alpha * alpha;
	return 2.0 * NoV / max(NoV + sqrt(a2 + (1.0 - a2) * NoV * NoV), 1e-7);
}

#endif
