#version 450

#extension GL_ARB_shader_draw_parameters : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : require
#extension GL_EXT_nonuniform_qualifier : require

#include "../include/set_bindings.glsl"
#include "../include/gpu_scene_structures.glsl"
#include "../include/pbr.glsl"

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inWorldPos;
layout(location = 4) flat in uint inMaterialID;

layout(location = 0) out vec4 outFragColor;

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

layout(push_constant) uniform DrawPushConstants {
	uint opaqueDrawCount;
	uint transparentDrawCount;
	uint totalVertexCount;
	uint totalIndexCount;
	uint totalMeshCount;
	uint totalMaterialCount;
	uint pad0[2];
} drawData;

const float PI = 3.14159265359;
const float MAX_REFLECTION_LOD = 4.0;
const uint DIFF_IRRA_MIP_LEVEL = 5;
const bool FLIP_ENVIRONMENT_MAP_Y = true;
const float MAX_AO_SATURATION = 4.0;

vec3 DiffuseIrradiance(vec3 N, uint diffuseIdx) {
	vec3 ENV_N = N;
	if (FLIP_ENVIRONMENT_MAP_Y) ENV_N.y = -ENV_N.y;
	return textureLod(envMaps[nonuniformEXT(diffuseIdx)], ENV_N, DIFF_IRRA_MIP_LEVEL).rgb;
}

vec3 SpecularReflection(vec3 V, vec3 N, float roughness, vec3 F, uint specularIdx, uint brdfIdx) {
	vec3 R = reflect(-V, N);
	if (FLIP_ENVIRONMENT_MAP_Y) R.y = -R.y;
	vec3 prefilteredColor = textureLod(envMaps[nonuniformEXT(specularIdx)], R, roughness * MAX_REFLECTION_LOD).rgb;
	vec2 envBRDF = texture(combinedSamplers[nonuniformEXT(brdfIdx)], vec2(max(dot(N, V), 0.0), roughness)).rg;
	return prefilteredColor * (F * envBRDF.x + envBRDF.y);
}

void main() {
	// fetch material data
	Material mat = MaterialBuffer(globalAddressTable.addrs[ABT_Material]).materials[inMaterialID];

	vec4 albedoMap = texture(combinedSamplers[nonuniformEXT(mat.albedoID)], inUV) * mat.colorFactor;
	vec4 mrSample  = texture(combinedSamplers[nonuniformEXT(mat.metalRoughnessID)], inUV);
	vec3 normalMap = texture(combinedSamplers[nonuniformEXT(mat.normalID)], inUV).rgb;
	float ao = texture(combinedSamplers[nonuniformEXT(mat.aoID)], inUV).r * mat.ambientOcclusion;
	vec3 emissiveTex = texture(combinedSamplers[nonuniformEXT(mat.emissiveID)], inUV).rgb;
	vec3 emissive = emissiveTex * mat.emissiveColor * mat.emissiveStrength;

	// Environment image indices for IBL
	uint diffuseIdx = uint(envMapSet.indices[0].x);
	uint specularIdx = uint(envMapSet.indices[0].y);
	uint brdfIdx = uint(envMapSet.indices[0].z);

	if (albedoMap.w < mat.alphaCutoff) discard;

	vec3 sampledNormal = normalize(normalMap * 2.0 - 1.0);
	vec3 normal = normalize(mix(inNormal, sampledNormal, mat.normalScale));

	vec3 lightColor = scene.sunlightColor.rgb * scene.sunlightColor.a;
	vec3 albedo = inColor * albedoMap.rgb;
	vec3 viewDir = normalize(scene.cameraPos.xyz - inWorldPos);
	vec3 lightDir = normalize(scene.sunlightDirection.xyz);
	vec3 H = normalize(viewDir + lightDir);

	float roughness = clamp(mrSample.g * mat.metalRoughFactors.y, 0.05, 1.0);
	float metallic = mrSample.r * mat.metalRoughFactors.x;

	float NDF = D_GGX(normal, H, roughness);
	float G = G_SCHLICKGGX_SMITH(normal, viewDir, lightDir, roughness);
	vec3 F0 = mix(vec3(0.04), albedo, metallic);
	vec3 F = F_SCHLICK(viewDir, H, F0);

	vec3 numerator = NDF * G * F;
	float denominator = 4.0 * max(dot(normal, viewDir), 0.0) * max(dot(normal, lightDir), 0.0);
	vec3 specular = numerator / max(denominator, 0.001);

	vec3 kS = F;
	vec3 kD = vec3(1.0) - kS;
	kD *= 1.0 - metallic;

	float NdotL = max(dot(normal, lightDir), 0.0);
	vec3 diffuse = Lambert(kD, albedo);

	vec3 irradiance = DiffuseIrradiance(normal, diffuseIdx);
	vec3 reflectionDiffuse = irradiance * albedo;
	vec3 reflectionSpecular = SpecularReflection(viewDir, normal, roughness, F, specularIdx, brdfIdx);

	vec3 ambient = kD * (reflectionDiffuse + reflectionSpecular);
	float sat_factor = mix(MAX_AO_SATURATION, 1, ao);
	albedo = pow(albedo, vec3(sat_factor));

	vec3 finalColor = (diffuse + specular) * lightColor * NdotL;
	vec3 correctedAmbient = ambient / (ambient + vec3(1.0));
	correctedAmbient = pow(correctedAmbient, vec3(1.0 / 2.2));
	finalColor += correctedAmbient + emissive;
	finalColor += vec3(0.5) * ao;

	//outFragColor = vec4(finalColor, albedoMap.w);

	//outFragColor = vec4(reflectionSpecular, albedoMap.w);
	//outFragColor = vec4(reflectionDiffuse, albedoMap.w);
	//outFragColor = vec4(irradiance, albedoMap.w);
	//outFragColor = vec4(diffuse, 1.0);
	//outFragColor = vec4(vec3(metallic), 1.0);
	//outFragColor = vec4(vec3(roughness), 1.0);
	//outFragColor = vec4(vec3(ao), 1.0);
	outFragColor = vec4(albedo, albedoMap.w);
	//outFragColor = vec4(normalize(reflect(-viewDir, normal)) * 0.5 + 0.5, 1.0);
	//outFragColor = vec4(sampledNormal, 1.0);
	//outFragColor = vec4(inUV, 0.0, 1.0);
	//outFragColor = vec4(inColor, 1.0);
	//outFragColor = vec4(emissive, 1.0);

//	vec3 debugColor = vec3(
//    float(inst.materialID & 0xFFu) / 255.0,
//    float((inst.materialID >> 8) & 0xFFu) / 255.0,
//    float((inst.materialID >> 16) & 0xFFu) / 255.0
//	);
//	outFragColor = vec4(debugColor, 1.0);
}