#ifndef RT_SHADOW_GLSL
#define RT_SHADOW_GLSL

#extension GL_GOOGLE_include_directive : require

#include "rt_geometry.glsl"
#include "rt_sampling.glsl"

struct RTVisibility
{
	float visibility;
	float occluderDist;
	float hits;
};

float rtTraceShadow(vec3 origin, vec3 dir, float tMin, float tMax)
{
	rayQueryEXT sq;
	rayQueryInitializeEXT(sq, sceneTLAS,
		gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT
		| gl_RayFlagsCullBackFacingTrianglesEXT,
		RT_MASK_SHADOW_CASTERS,
		origin, tMin, dir, tMax);
	rayQueryProceedEXT(sq);

	return rayQueryGetIntersectionTypeEXT(sq, true) == gl_RayQueryCommittedIntersectionNoneEXT
		 ? -1.0
		 : rayQueryGetIntersectionTEXT(sq, true);
}

float rtTraceShadowAlphaTested(vec3 origin, vec3 dir, float tMin, float tMax, float mipBias)
{
	rayQueryEXT sq;
	rayQueryInitializeEXT(sq, sceneTLAS,
		gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsCullBackFacingTrianglesEXT,
		RT_MASK_SHADOW_CASTERS,
		origin, tMin, dir, tMax);

	while (rayQueryProceedEXT(sq))
	{
		if (rayQueryGetIntersectionTypeEXT(sq, false) == gl_RayQueryCandidateIntersectionTriangleEXT)
		{
			uint instanceID = rayQueryGetIntersectionInstanceCustomIndexEXT(sq, false);
			uint primID     = rayQueryGetIntersectionPrimitiveIndexEXT(sq, false);
			vec2 bary       = rayQueryGetIntersectionBarycentricsEXT(sq, false);

			if (rtAlphaTestPasses(instanceID, primID, bary, mipBias))
				rayQueryConfirmIntersectionEXT(sq);
		}
	}

	return rayQueryGetIntersectionTypeEXT(sq, true) == gl_RayQueryCommittedIntersectionNoneEXT
		 ? -1.0
		 : rayQueryGetIntersectionTEXT(sq, true);
}

RTVisibility rtConeVisibility(
	vec3 origin, vec3 L, float tanRadius,
	vec2 pixel, int taps, float tMin, float tMax,
	float mipBias, bool alphaTested)
{
	RTVisibility r;
	r.visibility   = 1.0;
	r.occluderDist = 0.0;

	if (taps <= 0) return r;

	uint  frameIndex = getSceneData().temporal.x;

	float phi    = createPhase(pixel) + fract(float(frameIndex) * 0.7548776662);
	float jitter = fract(temporalInterleavedGradientNoise(pixel + 37.0, 0, 1.0)
					   + float(frameIndex) * 0.5698402909);

	mat3 basis = buildTBN(L);

	float vis     = 0.0;
	float distSum = 0.0;
	float hits    = 0.0;

	for (int i = 0; i < taps; ++i)
	{
		vec3  dir = rtConeDirection(basis, rtVogelDisk(i, taps, phi, jitter), tanRadius);
		float t   = alphaTested ? rtTraceShadowAlphaTested(origin, dir, tMin, tMax, mipBias)
								: rtTraceShadow(origin, dir, tMin, tMax);

		if (t < 0.0) vis += 1.0;
		else       { distSum += t; hits += 1.0; }
	}

	r.visibility   = vis / float(taps);
	r.occluderDist = hits > 0.0 ? distSum / hits : 0.0;
	return r;
}

RTVisibility rtSunVisibility(
	vec3 origin, vec3 L, float softness,
	vec2 pixel, int taps, float tMin, float tMax,
	float mipBias, bool alphaTested)
{
	return rtConeVisibility(origin, L, rtSunTanRadius(softness),
		pixel, taps, tMin, tMax, mipBias, alphaTested);
}


RTVisibility rtConeVisibilityRnd(
	vec3 origin, vec3 L, float tanRadius,
	vec2 rnd, int taps, float tMin, float tMax,
	float mipBias, bool alphaTested)
{
	RTVisibility r;
	r.visibility   = 1.0;
	r.occluderDist = 0.0;
	r.hits         = 0.0;

	if (taps <= 0) return r;

	mat3  basis  = buildTBN(L);
	float phi    = rnd.x * TWO_PI;
	float jitter = rnd.y;

	float vis     = 0.0;
	float distSum = 0.0;
	float hits    = 0.0;

	for (int i = 0; i < taps; ++i)
	{
		vec3  dir = rtConeDirection(basis, rtVogelDisk(i, taps, phi, jitter), tanRadius);
		float t   = alphaTested ? rtTraceShadowAlphaTested(origin, dir, tMin, tMax, mipBias)
								: rtTraceShadow(origin, dir, tMin, tMax);

		if (t < 0.0) vis += 1.0;
		else       { distSum += t; hits += 1.0; }
	}

	r.visibility   = vis / float(taps);
	r.occluderDist = hits > 0.0 ? distSum / hits : 0.0;
	r.hits         = hits;
	return r;
}

#endif
