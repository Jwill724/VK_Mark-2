#version 450

#extension GL_ARB_shader_draw_parameters : require
#extension GL_EXT_buffer_reference       : require
#extension GL_EXT_scalar_block_layout    : require
#extension GL_ARB_gpu_shader_int64       : require
#extension GL_GOOGLE_include_directive   : require
#extension GL_ARB_separate_shader_objects : require
#extension GL_EXT_nonuniform_qualifier   : require

#include "../include/set_bindings.glsl"
#include "../include/gpu_scene_structures.glsl"
#include "../include/pbr.glsl"

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inWorldPos;
layout(location = 4) flat in uint inMaterialID;

layout(location = 0) out vec4 outFragColor;

// === global tables/UBOs ===
layout(set = GLOBAL_SET, binding = ADDRESS_TABLE_BINDING, scalar) readonly buffer GlobalAddressTableBuffer {
    GPUAddressTable globalAddressTable;
};

layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_ENV_INDEX) uniform EnvMapData {
    EnvMapIndexArray envMapSet;
};

layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_SAMPLER_CUBE) uniform samplerCube envMaps[];
layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_COMBINED_SAMPLER) uniform sampler2D combinedSamplers[];

layout(set = FRAME_SET, binding = FRAME_BINDING_SCENE) uniform SceneUBO {
    SceneData scene;
};

const bool FLIP_ENV_Y = true;

// === IBL sampling helpers ===
vec3 sampleIrradiance(vec3 N, uint irrIdx)
{
    vec3 d = normalize(N);
    if (FLIP_ENV_Y) d.y = -d.y;
    // cosine-convolved cube: single mip
    return textureLod(envMaps[nonuniformEXT(irrIdx)], d, 0.0).rgb;
}

vec3 sampleSpecIBL(vec3 V, vec3 N, float roughness, vec3 F_ibl, uint specIdx, uint brdfIdx)
{
    vec3 R = reflect(-V, normalize(N));
    if (FLIP_ENV_Y) R.y = -R.y;

    int levels = textureQueryLevels(envMaps[nonuniformEXT(specIdx)]);
    float lod  = clamp(roughness * float(levels - 1), 0.0, float(levels - 1));
    vec3 prefiltered = textureLod(envMaps[nonuniformEXT(specIdx)], R, lod).rgb;

    float NdotV = max(dot(normalize(N), normalize(V)), 0.0);
    vec2  brdf  = texture(combinedSamplers[nonuniformEXT(brdfIdx)], vec2(NdotV, roughness)).rg;

    // split-sum approx
    return prefiltered * (F_ibl * brdf.x + brdf.y);
}

void main()
{
    // fetch material
    Material mat = MaterialBuffer(globalAddressTable.addrs[ABT_Material]).materials[inMaterialID];

    vec4  base   = texture(combinedSamplers[nonuniformEXT(mat.albedoID)], inUV) * mat.colorFactor;
    float ao     = texture(combinedSamplers[nonuniformEXT(mat.aoID)],     inUV).r * mat.ambientOcclusion;
    float rough  = texture(combinedSamplers[nonuniformEXT(mat.metalRoughnessID)], inUV).g * mat.metalRoughFactors.y;
    float metal  = texture(combinedSamplers[nonuniformEXT(mat.metalRoughnessID)], inUV).b * mat.metalRoughFactors.x;
    vec3  emissT = texture(combinedSamplers[nonuniformEXT(mat.emissiveID)], inUV).rgb;

    if (base.a < mat.alphaCutoff) discard;

    rough = clamp(rough, 0.04, 1.0);
    metal = clamp(metal, 0.0, 1.0);
    ao    = clamp(ao,    0.0, 1.0);

    // geometry basis (world space)
    vec3 N = normalize(inNormal);
    vec3 V = normalize(scene.cameraPos.xyz - inWorldPos);
    vec3 L = normalize(scene.sunlightDirection.xyz);
    vec3 H = normalize(V + L);

    float NdotV = max(dot(N,V), 0.0);
    float NdotL = max(dot(N,L), 0.0);
    float LdotH = max(dot(L, normalize(V + L)), 0.0);

    // material colors (linear)
    vec3 albedo    = inColor * base.rgb;
    vec3 emissive  = emissT * mat.emissiveColor * mat.emissiveStrength;

    // Disney/Frostbite direct lighting
    rough = SpecularAA(rough, N);
    vec3 F0   = mix(vec3(0.04), albedo, metal);  // conductor/metallic model
    vec3 diff = DisneyDiffuse(albedo, rough, NdotV, NdotL, LdotH);
    vec3 spec = BRDF_Specular(N, V, L, F0, rough);


    // multi-scatter energy compensation for direct spec
    uint irrIdx  = envMapSet.indices[0].x;
    uint specIdx = envMapSet.indices[0].y;
    uint brdfIdx = envMapSet.indices[0].z;

    vec2 dfg = texture(combinedSamplers[nonuniformEXT(brdfIdx)], vec2(NdotV, rough)).rg;
    spec *= MultiScatterEnergyComp(F0, dfg);

    vec3 direct = (diff + spec) * (scene.sunlightColor.rgb * scene.sunlightColor.a) * NdotL;

    // IBL ambient
    vec3 F_ibl  = F_SchlickRoughness(F0, NdotV, rough);      // roughness-aware Fresnel for ambient
    vec3 kS_ibl = F_ibl;
    vec3 kD_ibl = (1.0 - kS_ibl) * (1.0 - metal);            // no diffuse for metals

    vec3 iblDiff = sampleIrradiance(N, irrIdx) * albedo;
    vec3 iblSpec = sampleSpecIBL(V, N, rough, F_ibl, specIdx, brdfIdx);

    float specAO = SpecAO_Rough(ao, NdotV, rough);           // roughness-aware specular AO
    vec3 ambient = kD_ibl * iblDiff * ao + iblSpec * specAO;

    vec3 color = direct + ambient + emissive;
    outFragColor = vec4(color, base.a);
}