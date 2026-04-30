#ifndef PBR_GLSL
#define PBR_GLSL

float linearRough(float r) { return max(r * r, 0.001); }

// GGX (Trowbridge-Reitz) NDF
float D_GGX(vec3 N, vec3 H, float roughness)
{
	float a = linearRough(roughness);
	float a2 = a * a;
	float NdotH  = saturatePBR(dot(N,H));
	float NdotH2 = NdotH*NdotH;
	float denom  = (NdotH2 * (a2 - 1.0) + 1.0);
	return a2 / max(PI * denom * denom, 1e-6);
}

// Height-correlated Smith GGX visibility (Frostbite/UE style).
float V_SmithGGXCorrelated(float NdotV, float NdotL, float roughness)
{
	float a = linearRough(roughness);
	float a2 = a * a;

	float gv = NdotL * sqrt(a2 + (1.0 - a2) * NdotV * NdotV);
	float gl = NdotV * sqrt(a2 + (1.0 - a2) * NdotL * NdotL);
	return 0.5 / max(gv + gl, 1e-6);
}

// Schlick Fresnel (Unreal fast pow variant)
float FRESNEL_POWER_UNREAL(vec3 V, vec3 H) {
	float vdh = dot(V,H);
	return (-5.55473 * vdh - 6.98316) * vdh;
}
vec3 F_SCHLICK(vec3 V, vec3 H, vec3 F0)
{
	return F0 + (1.0 - F0) * pow(2.0, FRESNEL_POWER_UNREAL(V,H));
}

// Disney/Burley diffuse (what frostbite uses)
vec3 DisneyDiffuse(vec3 albedo, float linearRoughness, float NdotV, float NdotL, float LdotH)
{
	linearRoughness = clamp(linearRoughness, 0.0, 1.0);
	float energyBias   = mix(0.0, 0.5,  linearRoughness);
	float energyFactor = mix(1.0, 1.0 / 1.51, linearRoughness);
	float F_D90        = energyBias + 2.0 * LdotH * LdotH * linearRoughness;

	float F_L = 1.0 + (F_D90 - 1.0) * pow(1.0 - clamp(NdotL, 0.0, 1.0), 5.0);
	float F_V = 1.0 + (F_D90 - 1.0) * pow(1.0 - clamp(NdotV, 0.0, 1.0), 5.0);

	return albedo * (F_L * F_V * energyFactor) * (1.0/PI);
}

vec3 Lambert(vec3 kD, vec3 albedo) { return kD * albedo / PI; }

// full microfacet spec term for direct lights
vec3 BRDF_Specular(float NdotV, float NdotL, vec3 N, vec3 V, vec3 H, vec3 F0, float roughness)
{
	float D = D_GGX(N, H, roughness);
	float Vv = V_SmithGGXCorrelated(NdotV, NdotL, roughness);
	vec3 F = F_SCHLICK(V, H, F0);
	return (D * Vv) * F; // already includes the 1/(4 NdotV NdotL) via V
}

// Schlick with roughness for IBL (keeps rough metals from going too dark)
vec3 F_SchlickRoughness(vec3 F0, float NoV, float roughness)
{
	return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - clamp(NoV, 0.0, 1.0), 5.0);
}

// Roughness-aware specular AO (cheap, view-dependent)
float SpecAO_Conservative(float ao, float NdotV, float rough)
{
	// stronger on smooth, lighter on rough
	float p = mix(4.0, 1.5, rough);
	float v = max(NdotV, 0.1);
	// never < ao and =1 when ao=1
	float t = pow(clamp((ao + v - 1.0 + ao), p);
	return clamp(max(t, ao), 0.0, 1.0);
}

// Multi-scatter energy compensation (UE/Frostbite style)
vec3 MultiScatterEnergyComp(vec3 F0, vec2 brdf)
{
	// E_ss = single-scatter energy from brdf; keep metals from over-darkening
	vec3 E_ss  = F0 * brdf.x + brdf.y;
	vec3 F_avg = F0 + (1.0 - F0) * 0.047619;
	return 1.0 + F_avg * (1.0 - E_ss) / max(E_ss, 1e-3);
}

float MicroShadowVisibility(float NdotL, float occlusion)
{
	float ao = clamp(occlusion, 0.0, 1.0);
	float visibility = (NdotL + ao) / (1.0 + ao);
	return clamp(visibility, 0.0, 1.0);
}

#endif
