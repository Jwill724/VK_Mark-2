#version 450

#extension GL_ARB_shader_draw_parameters : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : require
#extension GL_EXT_nonuniform_qualifier : require

#include "../include/gpu_scene_structures.glsl"
#include "../include/pbr.glsl"

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inWorldPos;
layout(location = 4) flat in uint vDrawID;
layout(location = 5) flat in uint vInstanceID;

layout(location = 0) out vec4 outFragColor;

layout(set = 0, binding = 0, scalar) readonly buffer GlobalAddressTableBuffer {
    GPUAddressTable globalAddressTable;
};

layout(set = 0, binding = 1) uniform EnvMapData {
    EnvMapBindingSet envMapSet;
};

layout(set = 0, binding = 2) uniform samplerCube envMaps[];
layout(set = 0, binding = 4) uniform sampler2D combinedSamplers[];


layout(set = 1, binding = 0, scalar) readonly buffer FrameAddressTableBuffer {
    GPUAddressTable frameAddressTable;
};

layout(set = 1, binding = 1) uniform SceneUBO {
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
const uint DIFF_IRRA_MIP_LEVEL = 2;
const bool FLIP_ENVIRONMENT_MAP_Y = true;
const float MAX_AO_SATURATION = 10.0;

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
	Instance inst;

	if (vDrawID < drawData.opaqueDrawCount) {
		// Opaque instances
		OpaqueInstances instBuf = OpaqueInstances(frameAddressTable.addrs[ABT_OpaqueInstances]);
		inst = instBuf.opaqueInstances[vInstanceID];
	} else {
		// Transparent instances
		uint tIndex = vDrawID - drawData.opaqueDrawCount;
		if (tIndex >= drawData.transparentDrawCount) return;

		TransparentInstances instBuf = TransparentInstances(frameAddressTable.addrs[ABT_TransparentInstances]);
		inst = instBuf.transparentInstances[vInstanceID];
	}

	// fetch material data
	uint matID = min(inst.materialID, drawData.totalMaterialCount - 1);
	Material mat = MaterialBuffer(globalAddressTable.addrs[ABT_Material]).materials[matID];
//	uint matID = 1;
//	Material mat = MaterialBuffer(globalAddressTable.addrs[ABT_Material]).materials[matID];

	// Environment image indices for IBL
	uint diffuseIdx = uint(envMapSet.mapIndices[0].x);
	uint specularIdx = uint(envMapSet.mapIndices[0].y);
	uint brdfIdx = uint(envMapSet.mapIndices[0].z);

	vec4 albedoMap = texture(combinedSamplers[nonuniformEXT(mat.albedoID)], inUV) * mat.colorFactor;
	vec4 mrSample  = texture(combinedSamplers[nonuniformEXT(mat.metalRoughnessID)], inUV);
	vec3 normalMap = texture(combinedSamplers[nonuniformEXT(mat.normalID)], inUV).rgb;
	float ao = texture(combinedSamplers[nonuniformEXT(mat.aoID)], inUV).r * mat.ambientOcclusion;
	vec3 emissiveTex = texture(combinedSamplers[nonuniformEXT(mat.emissiveID)], inUV).rgb;
	vec3 emissive = emissiveTex * mat.emissiveColor * mat.emissiveStrength;

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

	outFragColor = vec4(finalColor, albedoMap.w);

	//outFragColor = vec4(reflectionSpecular, albedoMap.w);
	//outFragColor = vec4(reflectionDiffuse, albedoMap.w);
	//outFragColor = vec4(irradiance, albedoMap.w);
	//outFragColor = vec4(diffuse, 1.0);
	//outFragColor = vec4(vec3(metallic), 1.0);
	//outFragColor = vec4(vec3(roughness), 1.0);
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