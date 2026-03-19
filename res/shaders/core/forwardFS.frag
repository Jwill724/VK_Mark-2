#version 450

#extension GL_GOOGLE_include_directive    : require
#extension GL_ARB_separate_shader_objects : require
#extension GL_EXT_nonuniform_qualifier    : require

#include "../include/set_bindings.glsl"
#include "../include/gpu_scene_structures.glsl"
#include "../include/pbr.glsl"
#include "../include/shadow.glsl"
#include "../include/clustered.glsl"
#include "../include/depth.glsl"

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec4 inViewPos;
layout(location = 4) in vec3 inNormal;
layout(location = 5) flat in uint inMaterialID;

layout(location = 0) out vec4 outFragColor;

// === global table/UBOs ===
layout(set = GLOBAL_SET, binding = ADDRESS_TABLE_BINDING, scalar) readonly buffer GlobalAddressTableBuffer {
	GPUAddressTable globalAddressTable;
};

layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_ENV_INDEX) uniform EnvMapData {
	EnvMapIndexArray envMapSet;
};

layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_DEBUG_INLINE, scalar) uniform DebugData {
	DebugToggles debug;
};

layout(set = FRAME_SET, binding = FRAME_BINDING_CLUSTERED) uniform ClusteredUBO {
	ClusteredData clusteredData;
};

layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_SAMPLER_CUBE) uniform samplerCube envMaps[];

layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_COMBINED_SAMPLER) uniform sampler2D combinedSamplers[];

// === frame table/UBOs ===
layout(set = FRAME_SET, binding = ADDRESS_TABLE_BINDING, scalar) readonly buffer FrameAddressTableBuffer {
	GPUAddressTable frameAddressTable;
};

layout(set = FRAME_SET, binding = FRAME_BINDING_SCENE) uniform SceneUBO {
	SceneData scene;
};

layout(set = FRAME_SET, binding = FRAME_BINDING_CSM) uniform ShadowUBO {
	ShadowCSM csm;
};

// Only used for opaque shading
layout(set = PUSH_SET, binding = PUSH_BINDING_INPUT_1_TEX) uniform sampler2D aoFinal;
layout(set = PUSH_SET, binding = PUSH_BINDING_INPUT_2_TEX) uniform sampler2D contactShadowMask;
layout(set = PUSH_SET, binding = PUSH_BINDING_INPUT_3_TEX) uniform sampler2D bentNormals;

// Reusing push struct to get activeLights
layout(push_constant) uniform ForwardPush {
	uint activeLightCount;
	float pad0;
	float pad1;
	float pad2;
	mat4 flashlightVP;
} pc;

const bool FLIP_ENV_Y = true;

// ibl function headers
vec3 sampleIrradiance(vec3 N, uint irrIdx);
vec3 sampleSpecIBL(vec3 V, vec3 N, float roughness, vec3 F0, vec2 brdf, uint specIdx);

// Specular AA
// Reduce sparkling/aliasing of specular highlights caused by
// high-frequency normal variation
float SpecularAA(float roughness, vec3 N);

// Cluster shading headers
float radiusAttenuation(float distanceToLight, float radius);
float inverseSquareAttenuation(float distanceToLight);
float spotConeFactor(vec3 lightDirWS, vec3 L_ws, float innerCos, float outerCos);
float lightFadeFactor(float distanceToLight, float fadeStart, float fadeEnd);
// Camera-distance fade to stabilize far light density.
float lightCameraFade(LocalLight light, vec3 cameraPosWS);
vec3 evaluatePointLight(
	LocalLight light,
	vec3 worldPos,
	vec3 cameraPosWS,
	vec3 normalWS,
	vec3 viewDirWS,
	float NdotV,
	vec3 albedo,
	vec3 F0,
	float roughness,
	float aoTerm,
	out float outNdotL);
vec3 evaluateSpotLight(
	LocalLight light,
	vec3 worldPos,
	vec3 cameraPosWS,
	vec3 normalWS,
	vec3 viewDirWS,
	float NdotV,
	vec3 albedo,
	vec3 F0,
	float roughness,
	float aoTerm,
	out float outNdotL);

// Normal mapping headers
mat3 buildTBN_FromDerivatives(vec3 normalWS, vec3 worldPos, vec2 uv);
vec3 computeNormalMappedWS(vec3 geometricNormalWS, vec3 worldPos, vec2 uv, vec3 normalTex, float normalScale);

void main()
{
	// fetch material
	Material mat = MaterialBuffer(globalAddressTable.addrs[ABT_Material]).materials[inMaterialID];
	vec4 base = texture(combinedSamplers[nonuniformEXT(mat.albedoID)], inUV) * mat.colorFactor;
	const float alpha = base.a;
	if (alpha < mat.alphaCutoff) discard;

	vec2 screenspace_uv = (gl_FragCoord.xy) / scene.viewportSize.xy;

	// Right handed view on the -z
	float viewDepth = -inViewPos.z;

	// geometry basis (world space)
	vec3 geometricNormalWS = normalize(inNormal);
	vec3 N = geometricNormalWS;

	if ((mat.flags & MATERIAL_FLAG_HAS_NORMAL_MAP) != 0u) {
		vec2 uvDx = dFdx(inUV);
		vec2 uvDy = dFdy(inUV);

		vec3 normalTex = textureGrad(
			combinedSamplers[nonuniformEXT(mat.normalID)],
			inUV,
			uvDx,
			uvDy
		).xyz;

//		float normalLod = textureQueryLod(
//			combinedSamplers[nonuniformEXT(mat.normalID)],
//			inUV
//		).x;
//
//		float fadeStart = 1.5;
//		float fadeEnd = 2.0;
//
//		float normalFade = 1.0 - smoothstep(
//			fadeStart,
//			fadeEnd,
//			normalLod
//		);

		float normalScale = mat.normalScale;// * normalFade;

		N = computeNormalMappedWS(
			geometricNormalWS,
			inWorldPos,
			inUV,
			normalTex,
			normalScale
		);
	}

	if (DBG(showNormals)) RET(N * 0.5 + 0.5, 1.0);

	float ao = texture(combinedSamplers[nonuniformEXT(mat.aoID)], inUV).r * mat.ambientOcclusion;
	float rough = texture(combinedSamplers[nonuniformEXT(mat.metalRoughnessID)], inUV).g * mat.metalRoughFactors.y;
	float metal = texture(combinedSamplers[nonuniformEXT(mat.metalRoughnessID)], inUV).b * mat.metalRoughFactors.x;
	vec3 emissT = texture(combinedSamplers[nonuniformEXT(mat.emissiveID)], inUV).rgb;

	vec3 albedo = inColor * base.rgb;
	vec3 emissive = emissT * (mat.emissiveColor * mat.emissiveStrength);

	rough = SpecularAA(rough, N);
	metal = clamp(metal, 0.0, 1.0);

	if (DBG(showAlbedo))    RET(albedo, alpha);
	if (DBG(showEmissive))  RET(emissive, alpha);
	if (DBG(showMetallic))  RET(vec3(metal), alpha);
	if (DBG(showRoughness)) RET(vec3(rough), alpha);

	vec3 sunColor = scene.sunlightColor.rgb * scene.sunlightColor.a;
	vec3 V = normalize(scene.cameraPos.xyz - inWorldPos);
	vec3 L = normalize(scene.sunlightDirection.xyz);
	vec3 H = normalize(V + L);

	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	float LdotH = max(dot(L, H), 0.0);

	// Screen space ambient occlusion
	float aoTerm = ao;
	if (DBG(aoMode) && mat.passType == PASS_OPAQUE) {
		float aoFactor = texture(aoFinal, screenspace_uv).r;
		if(DBG(showAmbientOcclusion)) {
			RET(vec3(aoFactor), 1.0);
		}
		aoTerm *= aoFactor;
	}

	// Screen space contact shadow
	float contactShadows = 1.0;
	if (DBG(enableSSS) && DBG(enableShadows) && mat.passType == PASS_OPAQUE) {
		float sss = texture(contactShadowMask, screenspace_uv).r;
		if(DBG(showSSS)) {
			RET(vec3(sss), 1.0);
		}
		contactShadows = sss;
	}

	vec3 F0 = mix(vec3(0.04), albedo, metal);
	vec3 F = F_SchlickRoughness(F0, NdotV, rough);
	vec3 kD = (1.0 - F) * (1.0 - metal);

	// Disney/Frostbite direct lighting
	vec3 diff = DisneyDiffuse(albedo, rough, NdotV, NdotL, LdotH);
	vec3 spec = BRDF_Specular(NdotV, NdotL, N, V, H, F0, rough);

	// ENVIRONMENT INDICES
	const uint envMapID = debug.activeEnvMap;
	const uint irrIdx = envMapSet.indices[envMapID].x;
	const uint specIdx = envMapSet.indices[envMapID].y;
	const uint brdfIdx = envMapSet.indices[envMapID].z; // All using the same index

	// multi-scatter energy compensation for direct spec
	vec2 brdf = texture(combinedSamplers[nonuniformEXT(brdfIdx)], vec2(NdotV, rough)).rg;
	spec *= MultiScatterEnergyComp(F0, brdf);

	if (DBG(showDiffuse))  RET(diff * (sunColor) * NdotL, alpha);
	if (DBG(showSpecular)) RET(spec * (sunColor) * NdotL, alpha);

	// Shadows
	float shadow = 1.0;
	if (DBG(enableShadows) && mat.passType == PASS_OPAQUE) {
		const uint cascadeCount = uint(csm.params.z);
		const uint shadowMapID = uint(csm.params.y);
		const float shadowBias = csm.params.x;
		const float texel = csm.params.w;

		const uint maxCascade = cascadeCount - 1u;

		// cascade index for split
		uint cascadeIdx = cascadeViewDepthSplit(viewDepth, cascadeCount, csm.cascadeSplits);

		// Debug view for cascade splits
		if (DBG(showCascadeSplits)) {
			vec3 overlayColor = cascadeColor(cascadeIdx);
			const float overlayAlpha = 0.6;
			vec3 finalColor = mix(albedo, overlayColor, overlayAlpha);

			RET(finalColor, alpha);
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
			const float radius = csm.maxFilterRadiusTexels[cascadeIdx];
			const float bias = shadowBias * (0.25 + angleScale * 0.65);

			vec4 atlas = csm.atlasUV[cascadeIdx];
			vec2 atlasUV = shadowUV * atlas.xy + atlas.zw;

			float sA = PCFPoissonHigh(
				gl_FragCoord.xy,
				combinedSamplers[nonuniformEXT(shadowMapID)],
				atlasUV,
				curDepth,
				bias,
				texel,
				radius);

			shadow = sA;

			// https://github.com/Williscool13/WillEngineV3/blob/54ea902fc64796c1b88ae63e2ad2ffb0da957b21/shaders/shadow_functions.slang#L89
			// The code for comparing view depth with splits and the blend itself copied from this.
			// Blending between cascades for smooth transitions.
			if (cascadeIdx < maxCascade) {
				uint nextIdx = min(cascadeIdx + 1u, maxCascade);
				float blendStart = csm.cascadeSplits[nextIdx] * 1.1;
				float blendEnd = csm.cascadeSplits[cascadeIdx];

				if (viewDepth >= blendStart) {
					vec4 nextLightVP = csm.cascadeVP[nextIdx] * vec4(inWorldPos, 1.0);
					vec3 nextProjCoords = nextLightVP.xyz / nextLightVP.w;

					vec2 nextShadowUV = nextProjCoords.xy * 0.5 + 0.5;
					nextShadowUV.y = 1.0 - nextShadowUV.y;

					float nextDepth = nextProjCoords.z;

					if (!(nextShadowUV.x < 0.0 || nextShadowUV.x > 1.0 ||
						nextShadowUV.y   < 0.0 || nextShadowUV.y > 1.0 ||
						nextDepth        < 0.0 || nextDepth      > 1.0))
					{
						vec4 nextAtlas = csm.atlasUV[nextIdx];
						vec2 nextAtlasUV = nextShadowUV * nextAtlas.xy + nextAtlas.zw;
						const float nextRadius = csm.maxFilterRadiusTexels[nextIdx];

						float sB = PCFPoissonHigh(
							gl_FragCoord.xy,
							combinedSamplers[nonuniformEXT(shadowMapID)],
							nextAtlasUV,
							nextDepth,
							bias,
							texel,
							nextRadius);

						float blendFactor = smoothstep(blendStart, blendEnd, viewDepth);
						shadow = mix(sA, sB, blendFactor);
					}
				}
			}
			else {
				float blendStart = csm.cascadeSplits[cascadeIdx] * 0.98;
				float blendEnd = csm.cascadeSplits[cascadeIdx];
				if (viewDepth >= blendStart) {
					float blendFactor = smoothstep(blendStart, blendEnd, viewDepth);
					shadow = mix(sA, 1.0, blendFactor);
				}
			}
		}
	}

	// Cluster shading
	LightBuffer lightBuf = LightBuffer(frameAddressTable.addrs[ABT_Lights]);
	VisibleLightCount visibleCountBuf = VisibleLightCount(frameAddressTable.addrs[ABT_VisibleLightCount]);
	VisibleLightIDs visibleIDsBuf = VisibleLightIDs(frameAddressTable.addrs[ABT_VisibleLightIDs]);
	vec3 localLightColor = vec3(0.0);
	if (pc.activeLightCount > 0 && mat.passType == PASS_OPAQUE) {
		ClusterCounts countsBuf = ClusterCounts(frameAddressTable.addrs[ABT_ClusterCounts]);
		ClusterOffsets offsetsBuf = ClusterOffsets(frameAddressTable.addrs[ABT_ClusterOffsets]);
		ClusterLightIDs clusterLightIDsBuf = ClusterLightIDs(frameAddressTable.addrs[ABT_ClusterLightIDs]);

		ClusterGrid fragGrid = computeClusterGrid(
			gl_FragCoord.xy,
			viewDepth,
			uvec2(scene.viewportSize.xy),
			clusteredData.tileSizeX,
			clusteredData.tileSizeY,
			clusteredData.tileCountX,
			clusteredData.tileCountY,
			clusteredData.zSlices,
			scene.cameraClips.x,
			scene.cameraClips.y
		);

		const uint clusterIndex = fragGrid.clusterIndex;
		uint count = countsBuf.counts[clusterIndex];
		count = min(count, clusteredData.maxLightsPerCluster);
		const uint offset = offsetsBuf.offsets[clusterIndex];

		for (uint i = 0u; i < count; ++i) {
			const uint clusterLightID = clusterLightIDsBuf.lightIDs[offset + i];

			LocalLight light = lightBuf.lights[clusterLightID];

			float unusedNdotL = 0.0;
			if (light.lightType == LIGHT_TYPE_POINT) {
				localLightColor += evaluatePointLight(
					light,
					inWorldPos,
					scene.cameraPos.xyz,
					N,
					V,
					NdotV,
					albedo,
					F0,
					rough,
					aoTerm,
					unusedNdotL);
			}
			else if (light.lightType == LIGHT_TYPE_SPOT) {
				const bool isFlashLight = (light.flags & LIGHT_FLAG_FLASHLIGHT) != 0u;
				const bool isFlashLightOff = (light.flags & LIGHT_FLAG_FLASHLIGHT_OFF) != 0u;

				if (isFlashLight && isFlashLightOff) continue;

				float spotNdotL = 0.0;

				vec3 lightResult = evaluateSpotLight(
					light,
					inWorldPos,
					scene.cameraPos.xyz,
					N,
					V,
					NdotV,
					albedo,
					F0,
					rough,
					aoTerm,
					spotNdotL);

				bool castsShadow = (light.flags & LIGHT_FLAG_CASTS_SPOT_SHADOW) != 0u;
				// Shadow only for flashlight for now
				if (castsShadow && isFlashLight && !isFlashLightOff) {

					// Project world pos into flashlight clip
					vec4 flashlightSpacePos = pc.flashlightVP * vec4(inWorldPos, 1.0);
					vec3 flashlightProjCoords = flashlightSpacePos.xyz / flashlightSpacePos.w;

					vec2 flashlightShadowUV = flashlightProjCoords.xy * 0.5 + 0.5;
					flashlightShadowUV.y = 1.0 - flashlightShadowUV.y;

					float flashlightShadowZ = flashlightProjCoords.z;

					if (!(flashlightShadowUV.x < 0.0 || flashlightShadowUV.x > 1.0  ||
						  flashlightShadowUV.y < 0.0 || flashlightShadowUV.y > 1.0  ||
						  flashlightShadowZ    < 0.0 || flashlightShadowZ    > 1.0))
					{
						float angleScale = 1.0 - clamp(spotNdotL, 0.0, 1.0);

						float baseBias = 0.0001;
						float flashlightShadowBias = baseBias * (0.25 + angleScale * 0.65);
						float radiusTexels = 1.0;

						float shadowTerm = PCFPoissonHigh(
							gl_FragCoord.xy,
							combinedSamplers[nonuniformEXT(light.shadowMapID)],
							flashlightShadowUV,
							flashlightShadowZ,
							flashlightShadowBias,
							flashlightShadowTexel, // predefined const
							radiusTexels
						);

						lightResult *= shadowTerm;
					}


					// Cookie / gobo (projected spotlight mask)
					if (light.cookieTexID != 0xFFFFFFFFu) {

						float cookieGobo = 0.0;

						// Outside projection => no cookie contribution
						if (!(flashlightShadowUV.x < 0.0 || flashlightShadowUV.x > 1.0 ||
							  flashlightShadowUV.y < 0.0 || flashlightShadowUV.y > 1.0))
						{
							cookieGobo = texture(
								combinedSamplers[nonuniformEXT(light.cookieTexID)],
								flashlightShadowUV
							).r;
						}

						lightResult *= cookieGobo;
					}
				}

				localLightColor += lightResult;
			}
		}
	}

	// Direct sun light
	float microVisSun = MicroShadowVisibility(NdotL, aoTerm);
	vec3 direct = (diff + spec) * sunColor * NdotL * shadow * microVisSun * contactShadows;

	// IBL specular
	vec3 iblSpec = sampleSpecIBL(V, N, rough, F0, brdf, specIdx);

	// IBL diffuse
	vec3 irradianceN = N;
	// GTAO Bent Normals used only on diffuse
	if (DBG(aoMode) && mat.passType == PASS_OPAQUE) {
		vec4 bentSample = texture(bentNormals, screenspace_uv);
		vec3 bent = normalize(bentSample.rgb);
	    vec3 bentWS = normalize(mat3(scene.invView) * bent);

		// Keep bent normal in the same surface hemisphere
		float bentGeomDot = dot(bentWS, geometricNormalWS);
		if (bentGeomDot < 0.0) {
			bentWS = normalize(bentWS - geometricNormalWS * bentGeomDot);
		}

		float bentDeviation = 1.0 - saturate(dot(N, bentWS));

		// Cone confidence - wide cone = less occluded = less redirection needed
		float bentConeAngle = bentSample.a;
		float coneConfidence = 1.0 - saturate(bentConeAngle / HALF_PI);

		float bentBlend = bentDeviation * coneConfidence;
		bentBlend = clamp(bentBlend, 0.0, 0.8);

		vec3 blended = normalize(mix(N, bentWS, bentBlend));
		irradianceN = blended;

		if (DBG(showBentNormals)) {
			RET(vec3(irradianceN), 1.0);
		}
	}
	vec3 iblDiff = sampleIrradiance(irradianceN, irrIdx) * albedo;

	// Split ambient components
	vec3 ambientDiffuse = kD * (iblDiff * aoTerm);
	float specAO = SpecAO_Conservative(aoTerm, NdotV, rough);
	vec3 ambientSpecular = iblSpec * specAO;

	float skyFacing = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
	float skyOcclusion = mix(0.5, 1.0, skyFacing);

	ambientDiffuse *= skyOcclusion;
	vec3 ambient = ambientDiffuse + ambientSpecular;

	vec3 color = direct + localLightColor + ambient + emissive;
	outFragColor = vec4(color, 1.0);
}



// =========================================
// === IBL SAMPLING FUNCTION DEFINITIONS ===

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

float SpecularAA(float roughness, vec3 N)
{
	vec3 dndx = dFdx(N);
	vec3 dndy = dFdy(N);

	float normalVariance =
		dot(dndx, dndx) +
		dot(dndy, dndy);

	float filteredRoughness2 =
		roughness * roughness +
		normalVariance;

	return clamp(sqrt(filteredRoughness2), 0.04, 1.0);
}


// =============================================
// === CLUSTER SHADING FUNCTIONS DEFINITIONS ===

float radiusAttenuation(float distanceToLight, float radius)
{
	float x = distanceToLight / max(radius, 1e-6);
	float t = clamp(1.0 - x * x, 0.0, 1.0);
	//float t = 1.0 - clamp(x, 0.0, 1.0);
	return t * t;
}

float inverseSquareAttenuation(float distanceToLight)
{
	float d2 = max(distanceToLight * distanceToLight, 1e-4);
	return 1.0 / d2;
}

float spotConeFactor(vec3 lightDirWS, vec3 L_ws, float innerCos, float outerCos)
{
	// lightDirWS points "forward" from the light
	// L_ws points from surface -> light, so -L_ws points from light -> surface.
	float cosAngle = dot(normalize(lightDirWS), normalize(-L_ws));

	// Smoothstep from outer -> inner
	float denom = max(innerCos - outerCos, 1e-6);
	float t = clamp((cosAngle - outerCos) / denom, 0.0, 1.0);
	return t;
}

float lightFadeFactor(float distanceToLight, float fadeStart, float fadeEnd) {
	return 1.0 - smoothstep(fadeStart, fadeEnd, distanceToLight);
}

float lightCameraFade(LocalLight light, vec3 cameraPosWS)
{
	float cameraToLight = length(light.position - cameraPosWS);

	float fadeStartMul = 20.0;
	float fadeEndMul = 100.0;

	// Standard radius
	if (light.radius < 1.0) {
		// Small radius will fade out at futher distance
		fadeStartMul = 50.0;
		fadeEndMul = 150.0;
	}

	float fadeStart = max(light.radius * fadeStartMul, 0.0);
	float fadeEnd = max(light.radius * fadeEndMul, fadeStart + 1e-3);

	return lightFadeFactor(cameraToLight, fadeStart, fadeEnd);
}

vec3 evaluatePointLight(
	LocalLight light,
	vec3 worldPos,
	vec3 cameraPosWS,
	vec3 normalWS,
	vec3 viewDirWS,
	float NdotV,
	vec3 albedo,
	vec3 F0,
	float roughness,
	float aoTerm,
	out float outNdotL)
{
	vec3 toLight = light.position - worldPos;
	float distanceToLight = length(toLight);

	if (distanceToLight >= light.radius) {
		outNdotL = 0.0;
		return vec3(0.0);
	}

	vec3 L_ws = toLight / max(distanceToLight, 1e-6);
	vec3 H_ws = normalize(viewDirWS + L_ws);

	float NdotL = max(dot(normalWS, L_ws), 0.0);
	float LdotH = max(dot(L_ws, H_ws), 0.0);

	outNdotL = NdotL;
	if (NdotL <= 0.0) return vec3(0.0);

	float attRadius = radiusAttenuation(distanceToLight, light.radius);
	float attPhys = inverseSquareAttenuation(distanceToLight);

	float attenuation = attRadius * attPhys;

	vec3 radiance = light.color * light.intensity * attenuation;

	float fade = lightCameraFade(light, cameraPosWS);
	if (fade <= 0.0) return vec3(0.0);

	radiance *= fade;

	vec3 diff = DisneyDiffuse(albedo, roughness, NdotV, NdotL, LdotH);
	vec3 spec = BRDF_Specular(NdotV, NdotL, normalWS, viewDirWS, H_ws, F0, roughness);

	float microVis = MicroShadowVisibility(NdotL, aoTerm);

	return (diff + spec) * radiance * NdotL * microVis;
}

vec3 evaluateSpotLight(
	LocalLight light,
	vec3 worldPos,
	vec3 cameraPosWS,
	vec3 normalWS,
	vec3 viewDirWS,
	float NdotV,
	vec3 albedo,
	vec3 F0,
	float roughness,
	float aoTerm,
	out float outNdotL)
{
	vec3 toLight = light.position - worldPos;
	float distanceToLight = length(toLight);

	if (distanceToLight >= light.radius) {
		outNdotL = 0.0;
		return vec3(0.0);
	}

	vec3 L_ws = toLight / max(distanceToLight, 1e-6);

	float cone = spotConeFactor(light.direction, L_ws, light.innerCos, light.outerCos);
	if (cone <= 0.0) {
		outNdotL = 0.0;
		return vec3(0.0);
	}

	// Same as point, just multiply by cone
	float dummyNdotL = 0.0;
	vec3 pointResult = evaluatePointLight(
		light,
		worldPos,
		cameraPosWS,
		normalWS,
		viewDirWS,
		NdotV,
		albedo,
		F0,
		roughness,
		aoTerm,
		dummyNdotL);

	outNdotL = dummyNdotL;
	return pointResult * cone;
}


// ===========================================
// === NORMAL MAPPING FUNCTION DEFINITIONS ===

mat3 buildTBN_FromDerivatives(vec3 normalWS, vec3 worldPos, vec2 uv)
{
	vec3 dpdx = dFdx(worldPos);
	vec3 dpdy = dFdy(worldPos);

	vec2 dUVdx = dFdx(uv);
	vec2 dUVdy = dFdy(uv);

	vec3 tangentWS = dpdx * dUVdy.y - dpdy * dUVdx.y;

	float tangentLen2 = dot(tangentWS, tangentWS);

	// fallback frame
	vec3 fallbackTangent = normalize(
		abs(normalWS.y) < 0.999 ? cross(normalWS, vec3(0.0, 1.0, 0.0))
								: cross(normalWS, vec3(1.0, 0.0, 0.0))
	);
	vec3 fallbackBitangent = cross(normalWS, fallbackTangent);

	// stabilize / orthonormalize
	vec3 safeTangent = tangentWS - normalWS * dot(normalWS, tangentWS);
	float safeLen2 = dot(safeTangent, safeTangent);

	// Blend factor: 0 = fallback, 1 = derivative TBN
	float useDeriv = smoothstep(1e-12, 1e-8, min(tangentLen2, safeLen2));

	vec3 finalTangent = normalize(mix(fallbackTangent, safeTangent, useDeriv));
	vec3 finalBitangent = cross(normalWS, finalTangent);

	return mat3(finalTangent, finalBitangent, normalWS);
}

vec3 computeNormalMappedWS(vec3 geometricNormalWS, vec3 worldPos, vec2 uv, vec3 normalTex, float normalScale)
{
	mat3 tbn = buildTBN_FromDerivatives(geometricNormalWS, worldPos, uv);

	vec3 normalTS = normalTex * 2.0 - 1.0;

	normalTS.xy *= normalScale;
	normalTS = normalize(normalTS);

	return normalize(tbn * normalTS);
}
