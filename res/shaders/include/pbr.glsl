#ifndef PBR_GLSL
#define PBR_GLSL

vec3 Lambert(vec3 kD, vec3 albedo)
{
    return kD * albedo / 3.14159265359;
}

float D_GGX(vec3 N, vec3 H, float roughness){
    // Brian Karis says GGX NDF has a longer "tail" that appears more natural
    float a = roughness * roughness;
    float a2 = a * a; // disney reparameterization
    float NdotH = max(dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float premult = NdotH2 * (a2 - 1.0f) + 1.0f;
    float denom = 3.14159265359 * premult * premult;
    return num / denom;
}

float G_SCHLICKGGX(float NdotX, float k){
    float num = NdotX;
    float denom = NdotX * (1.0f - k) + k;
    return num / denom;
}


float G_SCHLICKGGX_SMITH(vec3 N, vec3 V, vec3 L, float roughness){
    // height correlated smith method

    // "Disney reduced hotness" - Brian Karis
    float r = (roughness + 1);
    float k = (r * r) / 8.0f;

    float NDotV = max(dot(N, V), 0.0f);
    float NDotL = max(dot(N, L), 0.0f);

    float ggx2 = G_SCHLICKGGX(NDotV, k);
    float ggx1 = G_SCHLICKGGX(NDotL, k);
    return ggx1 * ggx2;
}

float FRESNEL_POWER_UNREAL(vec3 V, vec3 H){
    float VDotH = dot(V, H);
    return (-5.55473 * VDotH - 6.98316) * VDotH;

}

vec3 F_SCHLICK(vec3 V, vec3 H, vec3 F0){
    float VdotH = max(dot(V, H), 0.0f);

    // classic
    //return F0 + (1.0f - F0) * pow(1.0f - VdotH, 5.0f);

    // unreal optimized
    return F0 + (1 - F0) * pow(2, FRESNEL_POWER_UNREAL(V, H));
}

#endif