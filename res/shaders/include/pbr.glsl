#ifndef PBR_GLSL
#define PBR_GLSL

// GGX (Trowbridge-Reitz) NDF
float D_GGX(vec3 N, vec3 H, float roughness)
{
	float a = LinearRough(roughness);
	float a2 = a * a;
	float NdotH  = saturate(dot(N,H));
	float NdotH2 = NdotH*NdotH;
	float denom  = (NdotH2 * (a2 - 1.0) + 1.0);
	return a2 / max(PI * denom * denom, 1e-6);
}

// Height-correlated Smith GGX visibility (Frostbite/UE style).
float V_SmithGGXCorrelated(float NdotV, float NdotL, float roughness)
{
	float a = LinearRough(roughness);
	float a2 = a * a;

	float gv = NdotL * sqrt(a2 + (1.0 - a2) * NdotV * NdotV);
	float gl = NdotV * sqrt(a2 + (1.0 - a2) * NdotL * NdotL);
	return 0.5 / max(gv + gl, 1e-6);
}

// Schlick Fresnel (Unreal fast pow variant)
float FRESNEL_POWER_UNREAL(vec3 V, vec3 H) {
	float vdh = saturate(dot(V, H));
	return (-5.55473 * vdh - 6.98316) * vdh;
}

vec3 F_SCHLICK(vec3 V, vec3 H, vec3 F0)
{
	float vdh  = saturate(dot(V, H));
	float f90  = saturate(dot(F0, vec3(50.0 * 0.33)));
	return F0 + (f90 - F0) * pow(2.0, FRESNEL_POWER_UNREAL(V, H));
}

// Disney/Burley diffuse (what frostbite uses)
vec3 DisneyDiffuse(vec3 albedo, float linearRoughness, float NdotV, float NdotL, float LdotH)
{
	linearRoughness    = clamp(linearRoughness, 0.0, 1.0);
	float energyBias   = mix(0.0, 0.5,  linearRoughness);
	float energyFactor = mix(1.0, 1.0 / 1.51, linearRoughness);
	float F_D90        = energyBias + 2.0 * LdotH * LdotH * linearRoughness;

	float F_L = 1.0 + (F_D90 - 1.0) * pow(1.0 - clamp(NdotL, 0.0, 1.0), 5.0);
	float F_V = 1.0 + (F_D90 - 1.0) * pow(1.0 - clamp(NdotV, 0.0, 1.0), 5.0);

	return albedo * (F_L * F_V * energyFactor) * (1.0 / PI);
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
	// never < ao and =1 when ao=1
	float t = saturate(pow(NdotV + ao, exp2(-16 * rough - 1)) - 1 + ao);
	return clamp(max(t, ao), 0.0, 1.0);
}

float DielectricF0(float ior, float specularFactor)
{
	float f = (ior - 1.0) / (ior + 1.0);
	return clamp(f * f * specularFactor, 0.02, 0.10);
}

// Multi-scatter energy compensation.
// DFG LUT (brdf.x/.y) is split-sum from Karis (UE4 2013) / Frostbite 2014.
// Compensation term is Fdez-Aguera 2019; F_avg/21 from Kulla-Conty 2017.
// Returns a multiplier to apply onto single-scatter spec (FssEss = F0*brdf.x + brdf.y).
vec3 MultiScatterEnergyComp(vec3 F0, vec2 brdf)
{
	float Ess  = brdf.x + brdf.y;                  // achromatic single-scatter energy
	float Ems  = 1.0 - Ess;                        // energy lost after one scatter
	vec3  Favg = F0 + (1.0 - F0) / 21.0;           // Kulla-Conty average Fresnel
	vec3  FmsEms = Ems * Favg / max(1.0 - Favg * Ems, 1e-4);
	return 1.0 + FmsEms;
}

#endif
