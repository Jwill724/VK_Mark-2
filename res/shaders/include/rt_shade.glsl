#ifndef RT_SHADE_GLSL
#define RT_SHADE_GLSL

#extension GL_GOOGLE_include_directive : require

#include "rt_params.glsl"
#include "pbr.glsl"
#include "lighting.glsl"

const float SKY_MAX_LOD = 4.0;

float rtMaxComp(vec3 v)
{
	return max(v.r, max(v.g, v.b));
}

vec3 rtSampleSky(vec3 dir, float pathRough, uint skyboxID)
{
	return SampleCubeLod(skyboxID, vec3(dir.x, -dir.y, dir.z), pathRough * SKY_MAX_LOD).rgb;
}

int rtReflectionShadowTaps(float pathRough)
{
	return pathRough < 0.10 ? 4
		 : pathRough < 0.35 ? 2
							: 1;
}

vec3 rtShadeAnalytic(HitSurface s, vec3 rayDir, vec2 pixel, int shadowTaps, RTShadeParams p)
{
	SceneData    sd    = getSceneData();
	DebugToggles debug = getDebugToggles();

	vec3 V = -rayDir;

	Surface hs = makeSurface(
		s.position, s.normal, s.position + V,
		s.albedo, s.metallic, s.roughness, s.mat, p.brdfID);

	vec3 L        = normalize(sd.sunlightDirection.xyz);
	vec3 sunColor = sd.sunlightColor.rgb * sd.sunlightColor.a;

	LightSample sun = evaluateDirectional(hs, L, sunColor);

	float shadow = 1.0;
	if (sun.NdotL > 0.0)
	{
		float bias = rtSurfaceBias(p.shadow, distance(sd.cameraPos.xyz, s.position));

		RTVisibility v = rtSunVisibility(
			s.position + s.normal * bias, L, p.shadow.sunSoftness,
			pixel, clamp(shadowTaps, 1, 16),
			p.shadow.rayTMin, p.shadow.rayTMax,
			p.shadow.mipBias, p.shadow.alphaTested != 0u);

		shadow = v.visibility;
	}

	vec3 direct = (sun.diffuse + sun.specular) * shadow;

	vec3 local = vec3(0.0);

	if (debug.activeLightCount > 0u)
	{
		LightBuffer lightBuf = getLightBuffer();
		uint evaluated = 0u;

		for (uint i = 0u; i < debug.activeLightCount && evaluated < p.maxLights; ++i)
		{
			LocalLight light = lightBuf.lights[i];

			if ((light.flags & LIGHT_FLAG_FLASHLIGHT_OFF) != 0u) continue;

			LightSample ls = evaluateLocalLight(light, hs, sd.cameraPos.xyz);
			if (ls.NdotL <= 0.0) continue;

			++evaluated;
			local += ls.diffuse + ls.specular;
		}
	}

	uint envSet = min(debug.activeEnvMap, MAX_ENV_SETS - 1u);
	vec3 irradiance = sampleIrradiance(s.normal, getSHIrradianceBuffer().shIrr[envSet].sh);

	vec3 ambientDiffuse  = hs.kD * hs.diffuseAlbedo * (1.0 / PI) * irradiance;
	vec3 ambientSpecular = sampleSpecIBL(hs.V, hs.N, hs.rough, hs.F0, hs.brdf, p.specularID)
						 * hs.multiScatter;

	return direct + local + (ambientDiffuse + ambientSpecular) * p.ambientScale + s.emissive;
}

#endif
