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
#include "../include/shadow.glsl"

layout(location = 0) in vec3 inNormal;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inWorldPos;
layout(location = 4) in vec4 inViewPos;
layout(location = 5) flat in uint inMaterialID;

layout(location = 0) out vec4 outFragColor;
//layout(location = 1) out vec4 outMaterialData;

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
layout(set = PUSH_SET, binding = PUSH_BINDING_INPUT_1_TEX) uniform sampler2D aoFinal;

const bool FLIP_ENV_Y = true;

// === IBL FUNCTIONS ===
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
	float lod = clamp(roughness * float(levels - 1), 0.0, float(levels - 1));
	vec3 prefiltered = textureLod(envMaps[nonuniformEXT(specIdx)], R, lod).rgb;
	return prefiltered * (F0 * brdf.x + brdf.y);
}

// Specular AA
// Reduce sparkling/aliasing of specular highlights caused by
// high-frequency normal variation
float SpecularAA(float roughness, vec3 N)
{
	vec3 dndx = dFdx(N);
	vec3 dndy = dFdy(N);
	float variance = max(dot(dndx,dndx), dot(dndy,dndy));
	float r2 = roughness * roughness + variance;
	return sqrt(saturate(r2));
}

void main()
{
	// fetch material
	Material mat = MaterialBuffer(globalAddressTable.addrs[ABT_Material]).materials[inMaterialID];

	vec4 base = texture(combinedSamplers[nonuniformEXT(mat.albedoID)], inUV) * mat.colorFactor;
	if (base.a < mat.alphaCutoff) discard;

	// geometry basis (world space)
	vec3 N = normalize(inNormal);

	if (DBG(showNormals)) RET(N * 0.5 + 0.5, 1.0);

	float ao = texture(combinedSamplers[nonuniformEXT(mat.aoID)], inUV).r * mat.ambientOcclusion;
	float rough = texture(combinedSamplers[nonuniformEXT(mat.metalRoughnessID)], inUV).g * mat.metalRoughFactors.y;
	float metal = texture(combinedSamplers[nonuniformEXT(mat.metalRoughnessID)], inUV).b * mat.metalRoughFactors.x;
	vec3 emissT = texture(combinedSamplers[nonuniformEXT(mat.emissiveID)], inUV).rgb;

	vec3 albedo = inColor * base.rgb;
	vec3 emissive = emissT * (mat.emissiveColor * mat.emissiveStrength);

	rough = clamp(rough, 0.04, 1.0);
	metal = clamp(metal, 0.0, 1.0);

	if (DBG(showAlbedo))    RET(albedo, base.a);
	if (DBG(showEmissive))  RET(emissive, base.a);
	if (DBG(showBakedAO))   RET(vec3(ao), base.a);
	if (DBG(showMetallic))  RET(vec3(metal), base.a);
	if (DBG(showRoughness)) RET(vec3(rough), base.a);

	vec2 uv = gl_FragCoord.xy / scene.viewportSize.xy;

	vec3 sunColor = scene.sunlightColor.rgb * scene.sunlightColor.a;
	vec3 V = normalize(scene.cameraPos.xyz - inWorldPos);
	vec3 L = normalize(scene.sunlightDirection.xyz);
	vec3 H = normalize(V + L);

	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	float LdotH = max(dot(L, H), 0.0);

	// SSAO/GTAO combine
	float aoTerm = ao;
	if (DBG(aoMode) && mat.passType == PASS_OPAQUE) {
		float aoFactor = texture(aoFinal, uv).r;
		if(DBG(showAmbientOcclusion)) {
			RET(vec3(aoFactor), 1.0);
		}
		aoTerm *= aoFactor;
	}

	// Disney/Frostbite direct lighting
	rough = SpecularAA(rough, N);
	vec3 F0 = mix(vec3(0.04), albedo, metal);
	vec3 diff = DisneyDiffuse(albedo, rough, NdotV, NdotL, LdotH);
	vec3 spec = BRDF_Specular(NdotV, NdotL, N, V, H, F0, rough);

	//float F0_scalar = max(max(F0.r, F0.g), F0.b);
	//float specWeight = max(F0, metal);

	// ENVIRONMENT INDICES
	const uint envMapID = debug.activeEnvMap;
	const uint irrIdx = envMapSet.indices[envMapID].x;
	const uint specIdx = envMapSet.indices[envMapID].y;
	const uint brdfIdx = envMapSet.indices[envMapID].z; // All using the same index

	// multi-scatter energy compensation for direct spec
	vec2 brdf = texture(combinedSamplers[nonuniformEXT(brdfIdx)], vec2(NdotV, rough)).rg;
	spec *= MultiScatterEnergyComp(F0, brdf);

	if (DBG(showDiffuse))  RET(diff * (sunColor) * NdotL, base.a);
	if (DBG(showSpecular)) RET(spec * (sunColor) * NdotL, base.a);

	// Shadows
	float shadow = 1.0;
	if (DBG(enableShadows) && mat.passType == PASS_OPAQUE) {
		const uint cascadeCount = uint(csm.params.z);
		const uint shadowMapID = uint(csm.params.y);
		const float shadowBias = csm.params.x;
		const float texel = csm.params.w;

		const uint maxCascade = cascadeCount - 1u;

		// Right handed view on the -z
		float viewDepth = -inViewPos.z;

		// cascade index for split
		uint cascadeIdx = cascadeViewDepthSplit(viewDepth, cascadeCount, csm.cascadeSplits);

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
			const float angleScale = 1.0 - NdotL;
			const float radius = csm.cascadeRadii[cascadeIdx];
			const float bias = shadowBias * (0.25 + angleScale * 0.65);

			float sA = PCFPoissonHigh(
				gl_FragCoord.xy,
				shadowMap[nonuniformEXT(shadowMapID)],
				shadowUV,
				cascadeIdx,
				curDepth,
				bias,
				texel,
				radius);

			float sB = sA;
			if (cascadeIdx < maxCascade) {
				uint nextIdx = min(cascadeIdx + 1u, maxCascade);
				sB = PCFPoissonHigh(
					gl_FragCoord.xy,
					shadowMap[nonuniformEXT(shadowMapID)],
					shadowUV,
					nextIdx,
					curDepth,
					bias,
					texel,
					csm.cascadeRadii[nextIdx]);
			}

			// Smooth cascade transition
			float splitFar = csm.cascadeSplits[cascadeIdx];

			// These scale factors provide the most stable transition without gaps
			float blendStart = splitFar * 0.99;
			float blendEnd = splitFar * 1.1;

			float blendT = smoothstep(blendStart, blendEnd, viewDepth);
			shadow = mix(sA, sB, clamp(blendT, 0.0, 1.0));
		}
	}

	// Direct sun light
	vec3 direct = (diff + spec) * sunColor * NdotL * shadow;

	// IBL specular
	vec3 iblSpec = sampleSpecIBL(V, N, rough, F0, brdf, specIdx);

	// IBL diffuse
	vec3 F_ibl = F_SchlickRoughness(F0, NdotV, rough);
	vec3 kD_ibl = (1.0 - F_ibl) * (1.0 - metal);
	vec3 iblDiff = sampleIrradiance(N, irrIdx) * albedo;

	float specAO = SpecAO_Conservative(aoTerm, NdotV, rough);
	vec3 ambient = kD_ibl * iblDiff * aoTerm + iblSpec * specAO;

	vec3 color = direct + ambient + emissive;
	outFragColor = vec4(color, base.a);

	//outMaterialData = vec4(rough, metal, F0_scalar, specWeight);
}