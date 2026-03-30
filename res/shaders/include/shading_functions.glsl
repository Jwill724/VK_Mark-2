#ifndef SHADING_FUNCTIONS_GLSL
#define SHADING_FUNCTIONS_GLSL

// =========================================
// === IBL SAMPLING FUNCTION DEFINITIONS ===

vec3 sampleIrradiance(vec3 N, uint irrIdx)
{
	N.y = -N.y;
	return SampleCubeLod(irrIdx, N, 0.0).rgb;
}

vec3 sampleSpecIBL(vec3 V, vec3 N, float roughness, vec3 F0, vec2 brdf, uint specIdx)
{
	vec3 R = reflect(-V, N);
	R.y = -R.y;

	int levels = SampleCubeQueryLevels(specIdx);
	float lod = clamp(roughness * float(levels - 1), 0.0, float(levels - 1));
	vec3 prefiltered = SampleCubeLod(specIdx, R, lod).rgb;

	return prefiltered * (F0 * brdf.x + brdf.y);
}

// Specular AA
// Reduce sparkling/aliasing of specular highlights caused by
// high-frequency normal variation
float SpecularAA(float roughness, vec3 N)
{
	vec3 dndx = dFdxFine(N);
	vec3 dndy = dFdyFine(N);

	float normalVariance = dot(dndx, dndx) + dot(dndy, dndy);

	float filteredRoughness2 = roughness * roughness + normalVariance;

	return clamp(sqrt(filteredRoughness2), 0.04, 1.0);
}


// =============================================
// === CLUSTER SHADING FUNCTIONS DEFINITIONS ===

float radiusAttenuation(float distanceToLight, float radius)
{
	float x = distanceToLight / max(radius, 1e-6);
	float t = clamp(1.0 - x * x, 0.0, 1.0);
	//float t = 1.0 - clamp(x, 0.0, 1.0);
	return t * t;
}

float inverseSquareAttenuation(float distanceToLight)
{
	float d2 = max(distanceToLight * distanceToLight, 1e-4);
	return 1.0 / d2;
}

float spotConeFactor(vec3 lightDirWS, vec3 L_ws, float innerCos, float outerCos)
{
	// lightDirWS points "forward" from the light
	// L_ws points from surface -> light, so -L_ws points from light -> surface.
	float cosAngle = dot(normalize(lightDirWS), normalize(-L_ws));

	// Smoothstep from outer -> inner
	float denom = max(innerCos - outerCos, 1e-6);
	float t = clamp((cosAngle - outerCos) / denom, 0.0, 1.0);
	return t;
}

float lightFadeFactor(float distanceToLight, float fadeStart, float fadeEnd) {
	return 1.0 - smoothstep(fadeStart, fadeEnd, distanceToLight);
}

float lightCameraFade(LocalLight light, vec3 cameraPosWS)
{
	float cameraToLight = length(light.position - cameraPosWS);

	float fadeStartMul = 20.0;
	float fadeEndMul = 100.0;

	// Standard radius
	if (light.radius < 1.0) {
		// Small radius will fade out at futher distance
		fadeStartMul = 50.0;
		fadeEndMul = 150.0;
	}

	float fadeStart = max(light.radius * fadeStartMul, 0.0);
	float fadeEnd = max(light.radius * fadeEndMul, fadeStart + 1e-3);

	return lightFadeFactor(cameraToLight, fadeStart, fadeEnd);
}

vec3 evaluatePointLight(
	LocalLight light,
	vec3 worldPos,
	vec3 cameraPosWS,
	vec3 normalWS,
	vec3 viewDirWS,
	float NdotV,
	vec3 albedo,
	vec3 F0,
	float roughness,
	float aoTerm,
	out float outNdotL)
{
	vec3 toLight = light.position - worldPos;
	float distanceToLight = length(toLight);

	if (distanceToLight >= light.radius) {
		outNdotL = 0.0;
		return vec3(0.0);
	}

	vec3 L_ws = toLight / max(distanceToLight, 1e-6);
	vec3 H_ws = normalize(viewDirWS + L_ws);

	float NdotL = max(dot(normalWS, L_ws), 0.0);
	float LdotH = max(dot(L_ws, H_ws), 0.0);

	outNdotL = NdotL;
	if (NdotL <= 0.0) return vec3(0.0);

	float attRadius = radiusAttenuation(distanceToLight, light.radius);
	float attPhys = inverseSquareAttenuation(distanceToLight);

	float attenuation = attRadius * attPhys;

	vec3 radiance = light.color * light.intensity * attenuation;

	float fade = lightCameraFade(light, cameraPosWS);
	if (fade <= 0.0) return vec3(0.0);

	radiance *= fade;

	vec3 diff = DisneyDiffuse(albedo, roughness, NdotV, NdotL, LdotH);
	vec3 spec = BRDF_Specular(NdotV, NdotL, normalWS, viewDirWS, H_ws, F0, roughness);

	float microVis = MicroShadowVisibility(NdotL, aoTerm);

	return (diff + spec) * radiance * NdotL * microVis;
}

vec3 evaluateSpotLight(
	LocalLight light,
	vec3 worldPos,
	vec3 cameraPosWS,
	vec3 normalWS,
	vec3 viewDirWS,
	float NdotV,
	vec3 albedo,
	vec3 F0,
	float roughness,
	float aoTerm,
	out float outNdotL)
{
	vec3 toLight = light.position - worldPos;
	float distanceToLight = length(toLight);

	if (distanceToLight >= light.radius) {
		outNdotL = 0.0;
		return vec3(0.0);
	}

	vec3 L_ws = toLight / max(distanceToLight, 1e-6);

	float cone = spotConeFactor(light.direction, L_ws, light.innerCos, light.outerCos);
	if (cone <= 0.0) {
		outNdotL = 0.0;
		return vec3(0.0);
	}

	// Same as point, just multiply by cone
	float dummyNdotL = 0.0;
	vec3 pointResult = evaluatePointLight(
		light,
		worldPos,
		cameraPosWS,
		normalWS,
		viewDirWS,
		NdotV,
		albedo,
		F0,
		roughness,
		aoTerm,
		dummyNdotL);

	outNdotL = dummyNdotL;
	return pointResult * cone;
}

#endif

