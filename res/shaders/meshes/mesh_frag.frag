#version 460

#extension GL_ARB_shader_draw_parameters : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_ARB_gpu_shader_int64 : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : require

#include "../include/input_structures.glsl"
#include "../include/pbr.glsl"

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inWorldPos;
layout(location = 4) flat in uint vDrawID;

layout(location = 0) out vec4 outFragColor;

layout(set = 0, binding = 0, std430) readonly buffer globalAddressTableBuffer {
    GPUAddressTable globalAddressTable;
};

layout(set = 1, binding = 0, std430) readonly buffer frameAddressTableBuffer {
    GPUAddressTable frameAddressTable;
};

layout(set = 1, binding = 1) uniform SceneUBO {
    SceneData scene;
};

layout(set = 0, binding = 1) uniform samplerCube envMaps[];
layout(set = 0, binding = 3) uniform sampler2D images[];

layout(push_constant) uniform PushConstants {
	DrawPushConstants pc;
};

const float PI = 3.14159265359;
const float MAX_REFLECTION_LOD = 4.0;
const uint DIFF_IRRA_MIP_LEVEL = 2;
const bool FLIP_ENVIRONMENT_MAP_Y = true;
const float MAX_AO_SATURATION = 10.0;

vec3 DiffuseIrradiance(vec3 N) {
	vec3 ENV_N = N;
	if (FLIP_ENVIRONMENT_MAP_Y) ENV_N.y = -ENV_N.y;
	return textureLod(envMaps[nonuniformEXT(uint(scene.envMapIndex.x))], ENV_N, DIFF_IRRA_MIP_LEVEL).rgb;
}

vec3 SpecularReflection(vec3 V, vec3 N, float roughness, vec3 F) {
	vec3 R = reflect(-V, N);
	if (FLIP_ENVIRONMENT_MAP_Y) R.y = -R.y;
	vec3 prefilteredColor = textureLod(envMaps[nonuniformEXT(uint(scene.envMapIndex.y))], R, roughness * MAX_REFLECTION_LOD).rgb;
	vec2 envBRDF = texture(images[nonuniformEXT(uint(scene.envMapIndex.z))], vec2(max(dot(N, V), 0.0), roughness)).rg;
	return prefilteredColor * (F * envBRDF.x + envBRDF.y);
}

void main()
{
	Material mat;

	uint drawID = vDrawID;

	if (drawID == 0) {
		mat = MaterialBuffer(globalAddressTable.materialBuffer).materials[pc.materialIndex];
	} else {
		IndirectBuffer cmdBuf = IndirectBuffer(frameAddressTable.indirectCmdBuffer);
		InstanceBuffer instBuf = InstanceBuffer(frameAddressTable.instanceBuffer);
		IndirectDrawCmd cmd = cmdBuf.cmds[drawID];
		Instance inst = instBuf.instances[cmd.instanceIndex];
		mat = MaterialBuffer(globalAddressTable.materialBuffer).materials[inst.materialIndex];
	}

	vec4 color = texture(images[nonuniformEXT(mat.albedoLUTIndex)], inUV) * mat.colorFactor;
	vec4 mrSample = texture(images[nonuniformEXT(mat.metalRoughLUTIndex)], inUV);
	vec3 normalMap = texture(images[nonuniformEXT(mat.normalLUTIndex)], inUV).rgb;
	float ao = texture(images[nonuniformEXT(mat.aoLUTIndex)], inUV).r * mat.ambientOcclusion;
	vec3 emissive = mat.emissiveColor * mat.emissiveStrength;

	if (color.a < mat.alphaCutoff) discard;

	vec3 sampledNormal = normalize(normalMap * 2.0 - 1.0);
	vec3 normal = normalize(mix(inNormal, sampledNormal, mat.normalScale));

	vec3 lightColor = scene.sunlightColor.rgb * scene.sunlightColor.a;
	vec3 albedo = inColor * color.rgb;
	vec3 viewDir = normalize(scene.cameraPos.xyz - inWorldPos);
	vec3 lightDir = normalize(scene.sunlightDirection.xyz);
	vec3 H = normalize(viewDir + lightDir);

	float roughness = clamp(mrSample.g * mat.metalRoughFactors.y, 0.05, 1.0);
	float metallic = mrSample.b * mat.metalRoughFactors.x;

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

	vec3 irradiance = DiffuseIrradiance(normal);
	vec3 reflectionDiffuse = irradiance * albedo;
	vec3 reflectionSpecular = SpecularReflection(viewDir, normal, roughness, F);

	vec3 ambient = kD * (reflectionDiffuse + reflectionSpecular);
	float sat_factor = mix(MAX_AO_SATURATION, 1, ao);
	albedo = pow(albedo, vec3(sat_factor));

	vec3 finalColor = (diffuse + specular) * lightColor * NdotL;
	vec3 correctedAmbient = ambient / (ambient + vec3(1.0));
	correctedAmbient = pow(correctedAmbient, vec3(1.0 / 2.2));
	finalColor += correctedAmbient + emissive;
	finalColor += vec3(0.5) * ao;

	outFragColor = vec4(finalColor, color.w);

	//outFragColor = vec4(reflection_specular, _col.w);
	//outFragColor = vec4(reflection_diffuse, _col.w);
	//outFragColor = vec4(diffuse, 1.0);
	//outFragColor = vec4(vec3(metallic), 1.0);
	//outFragColor = vec4(vec3(roughness), 1.0);
	//outFragColor = vec4(texSample.rgb, 1.0);
	//outFragColor = vec4(normalize(reflect(-viewDir, normal)) * 0.5 + 0.5, 1.0);
	//outFragColor = vec4(texSample.r, texSample.g, texSample.b, 1.0);
	//outFragColor = vec4(sampledNormal, 1.0);
	//outFragColor = vec4(emissive, 1.0);
}