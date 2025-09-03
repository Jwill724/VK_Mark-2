#version 450

#extension GL_ARB_shader_draw_parameters  : require
#extension GL_EXT_buffer_reference        : require
#extension GL_EXT_scalar_block_layout     : require
#extension GL_ARB_gpu_shader_int64        : require
#extension GL_GOOGLE_include_directive    : require
#extension GL_ARB_separate_shader_objects : require
#extension GL_EXT_nonuniform_qualifier    : require

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

layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_DEBUG_INLINE, scalar) uniform DebugData {
	DebugToggles debug;
};

layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_SAMPLER_CUBE) uniform samplerCube envMaps[];

layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_COMBINED_SAMPLER) uniform sampler2D combinedSamplers[];
layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_COMBINED_SAMPLER) uniform sampler2DArrayShadow shadowMap[];

layout(set = FRAME_SET, binding = FRAME_BINDING_SCENE) uniform SceneUBO {
	SceneData scene;
};

layout(set = FRAME_SET, binding = FRAME_BINDING_CSM) uniform ShadowUBO {
	ShadowCSM csm;
};

// Only used for opaque shading
layout(set = PUSH_SET, binding = PUSH_BINDING_INPUT_TEX) uniform sampler2D ssaoFinal;

const bool FLIP_ENV_Y = true;

// helpers
#define DBG(x) (debug.x != 0u)
#define RET(rgb,a) { outFragColor = vec4((rgb), (a)); return; }

vec3 sampleIrradiance(vec3 N, uint irrIdx)
{
	if (FLIP_ENV_Y) N.y = -N.y;
	return textureLod(envMaps[nonuniformEXT(irrIdx)], N, 0.0).rgb;
}

vec3 sampleSpecIBL(vec3 V, vec3 N, float roughness, vec3 F0, vec2 brdf, uint specIdx)
{
	vec3 R = reflect(-V, N);
	if (FLIP_ENV_Y) R.y = -R.y;

	int levels = textureQueryLevels(envMaps[nonuniformEXT(specIdx)]);
	float lod  = clamp(roughness * float(levels - 1), 0.0, float(levels - 1));
	vec3 prefiltered = textureLod(envMaps[nonuniformEXT(specIdx)], R, lod).rgb;
	return prefiltered * (F0 * brdf.x + brdf.y);
}

vec3 cascadeColor(uint i)
{
	const vec3 C[4] = vec3[](
		vec3(1,0,0),
		vec3(0,1,0),
		vec3(0,0,1),
		vec3(1,1,0)
	);
	return C[min(i, 3u)];
}

void main()
{
	// fetch material
	Material mat = MaterialBuffer(globalAddressTable.addrs[ABT_Material]).materials[inMaterialID];

	// geometry basis (world space)
	vec3 N = normalize(inNormal);

	if (DBG(showNormals)) RET(N * 0.5 + 0.5, 1.0);

	// base data
	vec4 base   = texture(combinedSamplers[nonuniformEXT(mat.albedoID)], inUV) * mat.colorFactor;
	float ao    = texture(combinedSamplers[nonuniformEXT(mat.aoID)],     inUV).r * mat.ambientOcclusion;
	float rough = texture(combinedSamplers[nonuniformEXT(mat.metalRoughnessID)], inUV).g * mat.metalRoughFactors.y;
	float metal = texture(combinedSamplers[nonuniformEXT(mat.metalRoughnessID)], inUV).b * mat.metalRoughFactors.x;
	vec3 emissT = texture(combinedSamplers[nonuniformEXT(mat.emissiveID)], inUV).rgb;

	if (base.a < mat.alphaCutoff) discard;

	if (DBG(showAlbedo))     RET(inColor * base.rgb, base.a);
	if (DBG(showEmissive))   RET(emissT * mat.emissiveColor * mat.emissiveStrength, base.a);
	if (DBG(showAO))         RET(vec3(ao), base.a);
	if (DBG(showMetallic))   RET(vec3(clamp(metal, 0.0, 1.0)), base.a);
	if (DBG(showRoughness))  RET(vec3(clamp(rough, 0.04, 1.0)), base.a);

	// SSAO only
	if (DBG(showSSAO)) {
		vec2 uv = gl_FragCoord.xy / scene.viewportSize.xy;
		float ssaoFactor = texture(ssaoFinal, uv).r;
		RET(vec3(ssaoFactor), 1.0);
	}

	float shadow = 1.0;
	if (DBG(enableShadows)) {
		// cascade index for split viz
		vec4 viewPos = scene.view * vec4(inWorldPos, 1.0);
		float viewDepth = -viewPos.z;
		const uint cascadeCount = uint(csm.params.z);

		uint cascadeIdx = cascadeCount - 1u;
		for (uint i = 0u; i < cascadeCount; ++i) {
			if (viewDepth < csm.cascadeSplits[i]) {
				cascadeIdx = i;
				break;
			}
		}
		if (DBG(showCascadeSplits)) RET(cascadeColor(cascadeIdx), 1.0);

		// shadow sample
		vec4 lightSpacePos = csm.cascadeVP[cascadeIdx] * vec4(inWorldPos, 1.0);
		vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
		projCoords = projCoords * 0.5 + 0.5;

		if (!(projCoords.x < 0.0 || projCoords.x > 1.0 ||
			  projCoords.y < 0.0 || projCoords.y > 1.0 ||
			  projCoords.z < 0.0 || projCoords.z > 1.0))
		{
			const uint shadowMapID = uint(csm.params.y);
			shadow = texture(
				shadowMap[nonuniformEXT(shadowMapID)],
				vec4(projCoords.xy, cascadeIdx, projCoords.z + csm.params.x)
			);
		}
	}

	// SSAO combine
	float aoFinal = ao;
	if (mat.passType == PASS_OPAQUE && DBG(enableSSAO)) {
		vec2 uv = gl_FragCoord.xy / scene.viewportSize.xy;
		float ssaoFactor = texture(ssaoFinal, uv).r;
		aoFinal = ao * ssaoFactor;
	}

	rough = clamp(rough, 0.04, 1.0);
	metal = clamp(metal, 0.0, 1.0);

	vec3 V = normalize(scene.cameraPos.xyz - inWorldPos);
	vec3 L = normalize(scene.sunlightDirection.xyz);
	vec3 H = normalize(V + L);

	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	float LdotH = max(dot(L, H), 0.0);

	vec3 albedo = inColor * base.rgb;
	vec3 emissive = emissT * mat.emissiveColor * mat.emissiveStrength;

	// Disney/Frostbite direct lighting
	rough = SpecularAA(rough, N);
	vec3 F0 = mix(vec3(0.04), albedo, metal);
	vec3 diff = DisneyDiffuse(albedo, rough, NdotV, NdotL, LdotH);
	vec3 spec = BRDF_Specular(N, V, L, H, F0, rough);

	const uint irrIdx  = envMapSet.indices[0].x;
	const uint specIdx = envMapSet.indices[0].y;
	const uint brdfIdx = envMapSet.indices[0].z;

	// multi-scatter energy compensation for direct spec
	vec2 brdf = texture(combinedSamplers[nonuniformEXT(brdfIdx)], vec2(NdotV, rough)).rg;
	spec *= MultiScatterEnergyComp(F0, brdf);

	if (DBG(showDiffuse))  RET(diff * (scene.sunlightColor.rgb * scene.sunlightColor.a) * NdotL, base.a);
	if (DBG(showSpecular)) RET(spec * (scene.sunlightColor.rgb * scene.sunlightColor.a) * NdotL, base.a);

	vec3 direct = (diff + spec) * (scene.sunlightColor.rgb * scene.sunlightColor.a) * NdotL * shadow;

	// IBL ambient
	vec3 F_ibl  = F_SchlickRoughness(F0, NdotV, rough);
	vec3 kD_ibl = (1.0 - F_ibl) * (1.0 - metal);

	vec3 iblDiff = sampleIrradiance(N, irrIdx) * albedo;
	vec3 iblSpec = sampleSpecIBL(V, N, rough, F0, brdf, specIdx) * 0.5;

	float specAO = SpecAO_Conservative(aoFinal, NdotV, rough);
	vec3 ambient = kD_ibl * iblDiff * aoFinal + iblSpec * specAO;

	vec3 color = direct + ambient + emissive;
	outFragColor = vec4(color, base.a);
}