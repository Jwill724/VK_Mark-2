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
layout(location = 4) in vec4 inViewPos;
layout(location = 5) flat in uint inMaterialID;

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
layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_COMBINED_SAMPLER) uniform sampler2DArray shadowMap[];

layout(set = FRAME_SET, binding = FRAME_BINDING_SCENE) uniform SceneUBO {
	SceneData scene;
};

layout(set = FRAME_SET, binding = FRAME_BINDING_CSM) uniform ShadowUBO {
	ShadowCSM csm;
};

// Only used for opaque shading
layout(set = PUSH_SET, binding = PUSH_BINDING_INPUT_1_TEX) uniform sampler2D ssaoFinal;

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
		vec3(1, 0, 0),
		vec3(0, 1, 0),
		vec3(0, 0, 1),
		vec3(1, 1, 0)
	);
	return C[min(i, 3u)];
}

// 8-tap Poisson disk in texels
const vec2 PD[8] = vec2[](
  vec2( 0.0, -0.5), vec2( 0.5,  0.0), vec2( 0.0,  0.5), vec2(-0.5,  0.0),
  vec2( 0.35, 0.35), vec2(-0.35, 0.35), vec2( 0.35,-0.35), vec2(-0.35,-0.35)
);

float PCFPoisson(sampler2DArray sm, vec2 uv, uint layer, float z, float bias, float texel)
{
	// hash
	float h = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898,78.233))) * 43758.5453);
	float ang = h * 6.2831853;
	mat2 R = mat2(cos(ang), -sin(ang), sin(ang), cos(ang));

	float s = 0.0;
	for (int i = 0; i < 8; ++i) {
		vec2 o = (R * PD[i]) * texel;
		float d = texture(sm, vec3(uv + o, float(layer))).r;
		s += float((z - bias) < d);
	}
	return s * (1.0 / 8.0);
}

float PCFShadow(sampler2DArray shadowMap, vec2 baseUV, uint layer, float curDepth, float bias, float texelSize, int kernelSize) {
	float shadow = 0.0;
	int samples = (2 * kernelSize + 1) * (2 * kernelSize + 1);
	vec2 texel = vec2(texelSize, texelSize);

	for (int x = -kernelSize; x <= kernelSize; ++x) {
		for (int y = -kernelSize; y <= kernelSize; ++y) {
			vec2 offset = vec2(float(x), float(y)) * texel;
			float sampledDepth = texture(shadowMap, vec3(baseUV + offset, float(layer))).r;
			if ((curDepth - bias) < sampledDepth) {
				shadow += 1.0;
			}
		}
	}
	return shadow / float(samples);
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

	if (mat.passType == PASS_OPAQUE && base.a < mat.alphaCutoff) discard;

	vec3 albedo = inColor * base.rgb;
	vec3 emissive = emissT * mat.emissiveColor * mat.emissiveStrength;

	rough = clamp(rough, 0.04, 1.0);
	metal = clamp(metal, 0.0, 1.0);

	if (DBG(showAlbedo))     RET(albedo, base.a);
	if (DBG(showEmissive))   RET(emissive, base.a);
	if (DBG(showAO))         RET(vec3(ao), base.a);
	if (DBG(showMetallic))   RET(vec3(metal), base.a);
	if (DBG(showRoughness))  RET(vec3(rough), base.a);

	vec2 uv = gl_FragCoord.xy / scene.viewportSize.xy;

	vec3 V = normalize(scene.cameraPos.xyz - inWorldPos);
	vec3 L = normalize(scene.sunlightDirection.xyz);
	vec3 H = normalize(V + L);

	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	float LdotH = max(dot(L, H), 0.0);

	// SSAO combine
	float aoFinal = ao;
	if (mat.passType == PASS_OPAQUE && DBG(enableSSAO)) {
		float ssaoFactor = texture(ssaoFinal, uv).r;
		if(DBG(showSSAO)) {
			RET(vec3(ssaoFactor), 1.0);
		}
		aoFinal *= ssaoFactor;
	}

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

	// Shadows
	float shadow = 1.0;
	if (DBG(enableShadows) && mat.passType == PASS_OPAQUE) {
		const uint cascadeCount = uint(csm.params.z);
		const uint shadowMapID = uint(csm.params.y);
		const float shadowBias = csm.params.x;
		const float texelSize = csm.params.w;

		const uint maxCascade = cascadeCount - 1u;

		// Right handed view on the -z
		const float viewDepth = -inViewPos.z;

		// cascade index for split
		uint cascadeIdx = maxCascade;
		for (uint i = 0u; i < cascadeCount; ++i) {
			if (viewDepth < csm.cascadeSplits[i]) {
				cascadeIdx = i;
				break;
			}
		}

		// Debug view for cascade splits
		if (DBG(showCascadeSplits)) {
			vec3 overlayColor = cascadeColor(cascadeIdx);
			const float overlayAlpha = 0.6;
			vec3 finalColor = mix(albedo, overlayColor, overlayAlpha);

			RET(finalColor, base.a);
		}

		// transform into light space
		vec4 lightSpacePos = csm.cascadeVP[cascadeIdx] * vec4(inWorldPos, 1.0);
		vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
		vec2 shadowUV = projCoords.xy * 0.5 + 0.5; // [-1, 1] to [0, 1]
		shadowUV.y = 1.0 - shadowUV.y; // Flip y orientation
		float curDepth = projCoords.z; // z already in [0, 1]

		if (!(shadowUV.x < 0.0 || shadowUV.x > 1.0  ||
			  shadowUV.y < 0.0 || shadowUV.y > 1.0  ||
			  curDepth   < 0.0 || curDepth   > 1.0))
		{
			uint nextIdx = min(cascadeIdx + 1u, maxCascade);

			float bias = shadowBias * (1.0 - NdotL) * (1.0 + float(cascadeIdx) * 0.1);

			bool isMaxCascade = false;
			float texel = texelSize;
			if (cascadeIdx == maxCascade) {
				texel *= 2.0; // wider footprint for distant cascade
				isMaxCascade = true;
			}

			float sA = PCFPoisson(shadowMap[nonuniformEXT(shadowMapID)], shadowUV, cascadeIdx, curDepth, bias, texel);

			float sB = sA;
			if (!isMaxCascade) {
				sB = PCFPoisson(shadowMap[nonuniformEXT(shadowMapID)], shadowUV, nextIdx, curDepth, bias, texel);
			}

			// Smooth cascade transition
			float splitFar = csm.cascadeSplits[cascadeIdx];
			float blendStart = splitFar * 0.99;
			float blendEnd  = splitFar * 1.1;
			float blendT = smoothstep(blendStart, blendEnd, viewDepth);

			shadow = mix(sA, sB, clamp(blendT, 0.0, 1.0));
		}
	}

	vec3 direct = (diff + spec) * (scene.sunlightColor.rgb * scene.sunlightColor.a) * NdotL * shadow;

	// IBL ambient
	vec3 F_ibl  = F_SchlickRoughness(F0, NdotV, rough);
	vec3 kD_ibl = (1.0 - F_ibl) * (1.0 - metal);

	vec3 iblDiff = sampleIrradiance(N, irrIdx) * albedo;
	vec3 iblSpec = sampleSpecIBL(V, N, rough, F0, brdf, specIdx);

	float specAO = SpecAO_Conservative(aoFinal, NdotV, rough);
	vec3 ambient = kD_ibl * iblDiff * aoFinal + iblSpec * specAO;

	vec3 color = direct + ambient + emissive;
	outFragColor = vec4(color, base.a);
}