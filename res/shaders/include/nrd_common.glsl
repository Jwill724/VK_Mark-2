#ifndef NRD_COMMON_GLSL
#define NRD_COMMON_GLSL

const vec3  NRD_HIT_DIST_PARAMS = vec3(3.0, 0.1, 20.0);
const float NRD_FP16_MAX = 65504.0;

vec4 NRD_PackNormalAndRoughness(vec3 N, float linearRoughness)
{
	N /= max(abs(N.x) + abs(N.y) + abs(N.z), 1e-8);

	vec3 r;
	r.y = N.y * 0.5 + 0.5;
	r.x = N.x * 0.5 + r.y;
	r.y -= N.x * 0.5;

	float roughness = max(linearRoughness, 1.5 / 512.0);
	r.z = (N.z < 0.0 ? -roughness : roughness) * 0.5 + 0.5;

	return vec4(r, 0.0);
}

float NRD_GetSpecMagicCurve(float roughness)
{
	float f = 1.0 - exp2(-200.0 * roughness * roughness);
	return f * sqrt(clamp(roughness, 0.0, 1.0));
}

float REBLUR_FrontEnd_GetNormHitDist(float hitDist, float viewZ, float linearRoughness)
{
	float smc = NRD_GetSpecMagicCurve(linearRoughness);
	float f = (NRD_HIT_DIST_PARAMS.x + abs(viewZ) * NRD_HIT_DIST_PARAMS.y)
			* mix(NRD_HIT_DIST_PARAMS.z, 1.0, smc);

	return clamp(hitDist / f, 0.0, 1.0);
}

vec3 NRD_LinearToYCoCg(vec3 color)
{
	return vec3(
		dot(color, vec3( 0.25, 0.5,  0.25)),
		dot(color, vec3( 0.5,  0.0, -0.5 )),
		dot(color, vec3(-0.25, 0.5, -0.25)));
}

vec3 NRD_YCoCgToLinear(vec3 color)
{
	float t = color.x - color.z;

	vec3 r;
	r.y = color.x + color.z;
	r.x = t + color.y;
	r.z = t - color.y;

	return max(r, vec3(0.0));
}

vec4 REBLUR_FrontEnd_PackRadianceAndNormHitDist(vec3 radiance, float normHitDist)
{
	bool bad = any(isnan(radiance)) || any(isinf(radiance));
	radiance = bad ? vec3(0.0) : clamp(radiance, vec3(0.0), vec3(NRD_FP16_MAX));

	normHitDist = (isnan(normHitDist) || isinf(normHitDist))
		? 0.0 : clamp(normHitDist, 0.0, 1.0);

	return vec4(NRD_LinearToYCoCg(radiance), normHitDist);
}

vec4 REBLUR_BackEnd_UnpackRadianceAndNormHitDist(vec4 data)
{
	return vec4(NRD_YCoCgToLinear(data.xyz), data.w);
}

float SIGMA_FrontEnd_PackPenumbra(float distanceToOccluder, float tanOfLightAngularRadius)
{
	float penumbraSize = distanceToOccluder * tanOfLightAngularRadius;
	float penumbraRadius = penumbraSize * 0.5;

	return distanceToOccluder >= NRD_FP16_MAX ? NRD_FP16_MAX : min(penumbraRadius, 32768.0);
}

float SIGMA_BackEnd_UnpackShadow(float shadow) { return shadow * shadow; }

#endif
