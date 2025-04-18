#version 450

#extension GL_GOOGLE_include_directive : require
#include "input_structures.glsl"

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec3 inColor;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec4 inWorldPos;

layout (location = 0) out vec4 outFragColor;

void main()
{
	vec3 albedo = inColor * texture(colorTex, inUV).rgb;

	float roughness = clamp(materialData.metal_rough_factors.y, 0.05, 1.0);
	float metallic = materialData.metal_rough_factors.x;

	vec3 normal = normalize(inNormal);
	vec3 viewDir = normalize(-inWorldPos.xyz);
	vec3 lightDir = normalize(sceneData.sunlightDirection.xyz);
	vec3 halfVec = normalize(lightDir + viewDir);

	// Diffuse Lambert
	float NdotL = max(dot(normal, lightDir), 0.0);
	vec3 diffuse = albedo / 3.14159; // Lambert BRDF

	// Specular
	vec3 f0 = mix(vec3(0.04), albedo, metallic);
	vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - max(dot(viewDir, halfVec), 0.0), 5.0);
	float NdotH = max(dot(normal, halfVec), 0.0);
	float specPower = pow(2.0, mix(1.0, 11.0, 1.0 - roughness));
//	float specPower = mix(64.0, 1.0, roughness);
	float spec = pow(NdotH, specPower);
	spec = clamp(spec, 0.0, 1.0);

	vec3 specular = fresnel * spec * (roughness * 0.5 + 0.5);

	// Final lighting
	vec3 lightColor = sceneData.sunlightColor.rgb;
	vec3 ambient = albedo * sceneData.ambientColor.rgb;

	vec3 lighting = (diffuse * (1.0 - metallic) + specular) * lightColor * NdotL;

	vec3 finalColor = lighting + ambient;

	// Gamma correction
	finalColor = pow(finalColor, vec3(1.0 / 2.2));

	outFragColor = vec4(finalColor, 1.0);
}