#ifndef PBR_GLSL
#define PBR_GLSL

const float PI = 3.14159265359;

float saturate(float x) { return clamp(x, 0.0, 1.0); }
float linearRough(float r) { return max(r*r, 0.001); }

// GGX (Trowbridge-Reitz) NDF
float D_GGX(vec3 N, vec3 H, float roughness)
{
    float a  = linearRough(roughness);
    float a2 = a*a;
    float NdotH  = saturate(dot(N,H));
    float NdotH2 = NdotH*NdotH;
    float denom  = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

// Height-correlated Smith GGX visibility (Frostbite/UE style)
// Returns G2 / (4 * NdotV * NdotL), i.e. the "V" factor you multiply by D and F.
float V_SmithGGXCorrelated(float NdotV, float NdotL, float roughness)
{
    float a  = linearRough(roughness);
    float a2 = a*a;

    float gv = NdotL * sqrt(a2 + (1.0 - a2) * NdotV*NdotV);
    float gl = NdotV * sqrt(a2 + (1.0 - a2) * NdotL*NdotL);
    return 0.5 / (gv + gl);
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
vec3 DisneyDiffuse(vec3 albedo, float roughness, float NdotV, float NdotL, float LdotH)
{
    float r = roughness;                           // Burley uses linear roughness (r)
    float F_D90 = 0.5 + 2.0 * LdotH * LdotH * r;
    float lightScatter = 1.0 + (F_D90 - 1.0) * pow(1.0 - NdotL, 5.0);
    float viewScatter  = 1.0 + (F_D90 - 1.0) * pow(1.0 - NdotV, 5.0);
    return (albedo / PI) * lightScatter * viewScatter;
}

vec3 Lambert(vec3 kD, vec3 albedo) { return kD * albedo / PI; }

// Specular AA
float SpecularAA(float roughness, vec3 N)
{
    vec3 dndx = dFdx(N);
    vec3 dndy = dFdy(N);
    float variance = max(dot(dndx,dndx), dot(dndy,dndy));
    float r2 = roughness*roughness + variance;
    return sqrt(saturate(r2));
}

// full microfacet spec term for direct lights
vec3 BRDF_Specular(vec3 N, vec3 V, vec3 L, vec3 F0, float roughness)
{
    vec3  H     = normalize(V + L);
    float NdotV = saturate(dot(N,V));
    float NdotL = saturate(dot(N,L));
    float D = D_GGX(N,H,roughness);
    float Vv = V_SmithGGXCorrelated(NdotV, NdotL, roughness);
    vec3  F = F_SCHLICK(V, H, F0);
    return (D * Vv) * F;   // already includes the 1/(4 NdotV NdotL) via V
}

// Scalar Schlick (handy for clearcoat or when F0 is scalar)
float F_SchlickScalar(float F0, float cosTheta)
{
    // same fast pow form as your vector version
    float x = (-5.55473 * cosTheta - 6.98316) * cosTheta;
    return F0 + (1.0 - F0) * exp2(x);
}

// Schlick with roughness for IBL (keeps rough metals from going too dark)
vec3 F_SchlickRoughness(vec3 F0, float NoV, float roughness)
{
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(1.0 - NoV, 5.0);
}

// Roughness-aware specular AO (cheap, view-dependent)
float SpecAO_Rough(float ao, float NdotV, float rough)
{
    // glossy: trust NdotV more; rough: trust AO more
    float t = rough * rough;
    float a = mix(ao, 1.0, t);
    return clamp(a + NdotV - 1.0, 0.0, 1.0);
}

// Multi-scatter energy compensation (UE/Frostbite style)
vec3 MultiScatterEnergyComp(vec3 F0, vec2 dfg)
{
    // E_ss = single-scatter energy from DFG; keep metals from over-darkening
    vec3 E_ss  = F0 * dfg.x + dfg.y;
    vec3 F_avg = F0 + (1.0 - F0) * 0.047619;
    return 1.0 + F_avg * (1.0 - E_ss) / max(E_ss, 1e-3);
}

#endif