#ifndef RT_PARAMS_GLSL
#define RT_PARAMS_GLSL

#extension GL_GOOGLE_include_directive : require

#include "rt_shadow.glsl"

struct RTShadowParams
{
	vec3 sunDirectionVS;
	float pad0;

	float rayTMin;
	float rayTMax;
	float rayBias;
	float normalBias;

	float sunSoftness;
	float mipBias;
	uint  taps;
	uint  alphaTested;
};

struct RTShadeParams
{
	RTShadowParams shadow;
	float ambientScale;
	uint  skyboxID;
	uint  brdfID;
	uint  specularID;
	uint  maxLights;
};

float rtSurfaceBias(RTShadowParams p, float dist)
{
	return p.normalBias + dist * p.rayBias;
}

#endif
