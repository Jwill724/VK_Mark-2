#version 450

#extension GL_GOOGLE_include_directive    : require
#extension GL_ARB_separate_shader_objects : require

#include "../include/clustered.glsl"
#include "../include/common.glsl"
#include "../include/depth.glsl"
#include "../include/shadow.glsl"
#include "../include/pbr.glsl"
#include "../include/shading_functions.glsl"

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec4 inViewPos;
layout(location = 4) in vec3 inNormal;
layout(location = 5) in vec3 inTangent;
layout(location = 6) in float inTangentW;
layout(location = 7) flat in uint inMaterialID;

layout(location = 0) out vec4 outFragColor;

layout(set = PUSH_SET, binding = PUSH_BINDING_READ_1) uniform sampler2D aoFinal;
layout(set = PUSH_SET, binding = PUSH_BINDING_READ_2) uniform sampler2D contactShadowMask;
layout(set = PUSH_SET, binding = PUSH_BINDING_READ_3) uniform sampler2D bentNormals;

// Reusing push struct to get activeLights
layout(push_constant) uniform ForwardPush {
	uint activeLightCount;
	float oitDepthScale;
	float pad0;
	float pad1;
	mat4 flashlightVP;
} pc;

void main()
{
	Material mat = getMaterialBuffer().materials[inMaterialID];

	const vec4  base  = SampleTexture(mat.albedoID, inUV) * mat.colorFactor;
	const float alpha = base.a;
	if (alpha < mat.alphaCutoff) discard;

	SceneData scene             = getSceneData();
	ClusteredData clusteredData = getClusteredData();
	DebugToggles debug          = getDebugToggles();

	vec2 screenspace_uv = gl_FragCoord.xy / vec2(scene.viewportSize.xy);

	// Right handed view on the -z
	const float viewDepth = -inViewPos.z;

	// geometry basis (world space)
	const vec3 geometricNormalWS = normalize(inNormal);
	vec3 N                       = geometricNormalWS;

	if ((mat.flags & MATERIAL_FLAG_HAS_NORMAL_MAP) != 0u) {
		vec3 normalTex = SampleTexture(mat.normalID, inUV).rgb;

		vec3 T   = normalize(inTangent - geometricNormalWS * dot(geometricNormalWS, inTangent));
		vec3 B   = cross(geometricNormalWS, T) * inTangentW;
		mat3 tbn = mat3(T, B, geometricNormalWS);

		vec3 normalTS = normalTex * 2.0 - 1.0;
		normalTS.xy  *= mat.normalScale;
		normalTS      = normalize(normalTS);

		N = normalize(tbn * normalTS);
	}

	if (DBG(showNormals)) RET(N * 0.5 + 0.5, 1.0);

	vec3  albedo = inColor * base.rgb;
	float ao     = SampleTexture(mat.aoID,             inUV).r * mat.ambientOcclusion;
	float rough  = SampleTexture(mat.metalRoughnessID, inUV).g * mat.metalRoughFactors.y;
	float metal  = SampleTexture(mat.metalRoughnessID, inUV).b * mat.metalRoughFactors.x;
	vec3  emissT = SampleTexture(mat.emissiveID,       inUV).rgb;

	vec3  emissive = emissT * (mat.emissiveColor * mat.emissiveStrength);
	float lum      = max(max(emissive.r, emissive.g), emissive.b);
	// Boost only bright parts
	float boost    = smoothstep(1.0, 10.0, lum);
	emissive      *= mix(1.0, 3.0, boost);

	rough = SpecularAA(rough, N);
	metal = clamp(metal, 0.0, 1.0);

	if (DBG(showAlbedo))    RET(albedo,      alpha);
	if (DBG(showEmissive))  RET(emissive,    alpha);
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
	if (DBG(aoMode)) {
		float aoFactor = texture(aoFinal, screenspace_uv).r;
		if(DBG(showAmbientOcclusion)) {
			RET(vec3(aoFactor), 1.0);
		}
		aoTerm *= aoFactor;
	}

	// Screen space contact shadow
	float contactShadows = 1.0;
	if (DBG(enableSSS) && DBG(enableShadows)) {
		float sss = texture(contactShadowMask, screenspace_uv).r;
		if(DBG(showSSS)) {
			RET(vec3(sss), 1.0);
		}
		contactShadows = sss;
	}

	vec3 F0 = mix(vec3(0.04), albedo, metal);
	vec3 F  = F_SchlickRoughness(F0, NdotV, rough);
	vec3 kD = (1.0 - F) * (1.0 - metal);

	// Disney/Frostbite direct lighting
	vec3 diff = DisneyDiffuse(albedo, rough, NdotV, NdotL, LdotH);
	vec3 spec = BRDF_Specular(NdotV, NdotL, N, V, H, F0, rough);

	// ENVIRONMENT INDICES
	const uint             envMapID    = debug.activeEnvMap;
	const EnvMapIndexArray envMapArray = getEnvIdxArray();
	const uint             irrIdx      = envMapArray.indices[envMapID].x;
	const uint             specIdx     = envMapArray.indices[envMapID].y;
	const uint             brdfIdx     = envMapArray.indices[envMapID].z; // All using the same index

	// multi-scatter energy compensation for direct spec
	vec2 brdf = SampleTexture(brdfIdx, vec2(NdotV, rough)).rg;
	spec *= MultiScatterEnergyComp(F0, brdf);

	if (DBG(showDiffuse))  RET(diff * (sunColor) * NdotL, alpha);
	if (DBG(showSpecular)) RET(spec * (sunColor) * NdotL, alpha);

	// Shadows
	float shadow     = 1.0;
	mat2  shadowHash = mat2(1.0);
	if (debug.aaMode != AA_TAA) {
		shadowHash = createHash(gl_FragCoord.xy);
	}
	else {
		shadowHash = createHashTemporal(gl_FragCoord.xy, scene.temporal.x);
	}
	if (DBG(enableShadows)) {
		ShadowCSM   csm          = getShadowCSM();
		const uint  cascadeCount = uint(csm.params.z);
		const uint  shadowMapID  = uint(csm.params.y);
		//const float shadowBias   = csm.params.x;
		const float texel        = csm.params.w;

		// cascade index for split
		uint cascadeIdx = cascadeViewDepthSplit(viewDepth, cascadeCount, csm.cascadeSplits);

		const float angleScale = (0.25 + (1.0 - NdotL) * 0.65);
		const float radius     = csm.maxFilterRadiusTexels[cascadeIdx];
		const float shadowBias = csm.cascadeBias[cascadeIdx];
		const float bias       = max(shadowBias * angleScale, MIN_SHADOW_BIAS);

//		const float normalOffset  = csm.cascadeNormalOffset[cascadeIdx];
//		float NdotLRaw    = dot(geometricNormalWS, L);       // signed, not clamped
//		float offsetScale = clamp(1.0 - NdotLRaw, 0.0, 1.0); // 0 on lit face, 1 on dark face
//		vec3 offsetPos    = inWorldPos + geometricNormalWS * (normalOffset * offsetScale);

		// Debug view for cascade splits
		if (DBG(showCascadeSplits)) {
			vec3        overlayColor = cascadeColor(cascadeIdx);
			const float overlayAlpha = 0.6;
			vec3        finalColor   = mix(albedo, overlayColor, overlayAlpha);

			RET(finalColor, alpha);
		}

		// transform into light space
		vec4 lightSpacePos = csm.cascadeVP[cascadeIdx] * vec4(inWorldPos, 1.0);
		vec3 projCoords    = lightSpacePos.xyz / lightSpacePos.w;
		vec2 shadowUV      = projCoords.xy * 0.5 + 0.5;           // [-1, 1] to [0, 1]
		shadowUV.y         = 1.0 - shadowUV.y;                    // Flip y orientation
		float curDepth     = projCoords.z;                        // z already in [0, 1]

		if (!(shadowUV.x < 0.0 || shadowUV.x > 1.0 ||
			  shadowUV.y < 0.0 || shadowUV.y > 1.0 ||
			  curDepth   < 0.0 || curDepth   > 1.0))
		{
			vec4 atlas   = csm.atlasUV[cascadeIdx];
			vec2 atlasUV = shadowUV * atlas.xy + atlas.zw;

			vec2 atlasMin = atlas.zw;
			vec2 atlasMax = atlas.zw + atlas.xy;

			float sA = PCFVogel(
				shadowHash,
				shadowMapID,
				atlasUV,
				curDepth,
				bias,
				texel,
				radius,
				atlasMin,
				atlasMax);

			shadow = sA;

			// https://github.com/Williscool13/WillEngineV3/blob/54ea902fc64796c1b88ae63e2ad2ffb0da957b21/shaders/shadow_functions.slang#L89
			// The code for comparing view depth with splits and the blend itself copied from this.
			// Blending between cascades for smooth transitions.
			if (cascadeIdx < MAX_CASCADES_INDEX) {
				uint  nextIdx    = min(cascadeIdx + 1u, MAX_CASCADES_INDEX);
				float blendEnd   = csm.cascadeSplits[cascadeIdx];
				float blendStart = blendEnd * 0.90;

				if (viewDepth >= blendStart) {
					const float nextRadius = csm.maxFilterRadiusTexels[nextIdx];
//					const float nextNormalOffset  = csm.cascadeNormalOffset[nextIdx];
//					vec3 nextOffsetPos = inWorldPos + geometricNormalWS * (nextNormalOffset * offsetScale);
					vec4 nextLightVP = csm.cascadeVP[nextIdx] * vec4(inWorldPos, 1.0);
					vec3 nextProjCoords = nextLightVP.xyz / nextLightVP.w;

					vec2 nextShadowUV = nextProjCoords.xy * 0.5 + 0.5;
					nextShadowUV.y = 1.0 - nextShadowUV.y;

					float nextDepth = nextProjCoords.z;

					if (!(nextShadowUV.x < 0.0 || nextShadowUV.x > 1.0 ||
						nextShadowUV.y   < 0.0 || nextShadowUV.y > 1.0 ||
						nextDepth        < 0.0 || nextDepth      > 1.0))
					{
						vec4 nextAtlas         = csm.atlasUV[nextIdx];
						vec2 nextAtlasUV       = nextShadowUV * nextAtlas.xy + nextAtlas.zw;
						const float nextBias   = max(csm.cascadeBias[nextIdx] * angleScale, MIN_SHADOW_BIAS);

						vec2 nextAtlasMin = nextAtlas.zw;
						vec2 nextAtlasMax = nextAtlas.zw + nextAtlas.xy;

						float sB = PCFVogel(
							shadowHash,
							shadowMapID,
							nextAtlasUV,
							nextDepth,
							nextBias,
							texel,
							nextRadius,
							nextAtlasMin,
							nextAtlasMax);

						// smoothstep goes 0 at blendStart -> 1 at blendEnd,
						float blendFactor = smoothstep(blendStart, blendEnd, viewDepth);
						shadow = mix(sA, sB, blendFactor);
					}
				}
			}
			else {
				// Last cascade: fade to fully lit at the far edge to avoid
				// a hard shadow cutoff at the cascade boundary.
				float blendEnd   = csm.cascadeSplits[cascadeIdx];
				float blendStart = blendEnd * 0.98;
				if (viewDepth >= blendStart) {
					float blendFactor = smoothstep(blendStart, blendEnd, viewDepth);
					shadow = mix(sA, 1.0, blendFactor);
				}
			}
		}
	}

	// Cluster shading
	LightBuffer       lightBuf        = getLightBuffer();
	VisibleLightCount visibleCountBuf = getVisibleLightCountBuffer();
	VisibleLightIDs   visibleIDsBuf   = getVisibleLightIDsBuffer();

	vec3 localLightColor = vec3(0.0);
	if (pc.activeLightCount > 0u) {
		ClusterCounts   countsBuf          = getClusterCountsBuffer();
		ClusterOffsets  offsetsBuf         = getClusterOffsetsBuffer();
		ClusterLightIDs clusterLightIDsBuf = getClusterLightIDsBuffer();

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
		uint count              = countsBuf.counts[clusterIndex];
		count                   = min(count, clusteredData.maxLightsPerCluster);
		const uint offset       = offsetsBuf.offsets[clusterIndex];

		for (uint i = 0u; i < count; ++i) {
			const uint clusterLightID = clusterLightIDsBuf.lightIDs[offset + i];

			LocalLight light  = lightBuf.lights[clusterLightID];
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

				float spotMicroVis = MicroShadowVisibility(spotNdotL, aoTerm);
				lightResult *= spotMicroVis;

				bool castsShadow = (light.flags & LIGHT_FLAG_CASTS_SPOT_SHADOW) != 0u;
				// Shadow only for flashlight for now
				if (castsShadow && isFlashLight && !isFlashLightOff) {

					// Project world pos into flashlight clip
					vec4 flashlightSpacePos   = pc.flashlightVP * vec4(inWorldPos, 1.0);
					vec3 flashlightProjCoords = flashlightSpacePos.xyz / flashlightSpacePos.w;

					vec2 flashlightShadowUV = flashlightProjCoords.xy * 0.5 + 0.5;
					flashlightShadowUV.y    = 1.0 - flashlightShadowUV.y;

					float flashlightShadowZ = flashlightProjCoords.z;

					if (!(flashlightShadowUV.x < 0.0 || flashlightShadowUV.x > 1.0  ||
						  flashlightShadowUV.y < 0.0 || flashlightShadowUV.y > 1.0  ||
						  flashlightShadowZ    < 0.0 || flashlightShadowZ    > 1.0))
					{
						float angleScale = 1.0 - clamp(spotNdotL, 0.0, 1.0);

						float flashlightShadowBias = MIN_SHADOW_BIAS * (0.25 + angleScale * 0.65);
						float radiusTexels = 1.0;

						float shadowTerm = PCFPoissonLow(
							shadowHash,
							light.shadowMapID,
							flashlightShadowUV,
							flashlightShadowZ,
							flashlightShadowBias,
							flashlightShadowTexel
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
							float rawCookie = SampleTexture(light.cookieTexID, flashlightShadowUV).r;
							cookieGobo      = pow(rawCookie, 2.0);
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
	// Bent Normals used only on diffuse
	if (debug.aoMode == AO_VBAO_BENT_NORMALS) {
		vec4 bentSample      = texture(bentNormals, screenspace_uv);
		vec3 bentWS          = normalize(bentSample.rgb);
		float bentConeAngle  = bentSample.a;

		float bentDeviation = 1.0 - saturate(dot(N, bentWS));

		// Cone confidence - wide cone = less occluded = less redirection needed
		float coneConfidence = 1.0 - saturate(bentConeAngle / HALF_PI);

		float bentBlend = bentDeviation * coneConfidence;
		bentBlend       = clamp(bentBlend, 0.0, 0.5);

		if (DBG(showBentNormals)) {
			RET(vec3(bentBlend), 1.0);
		}

		vec3 blended = normalize(mix(N, bentWS, bentBlend));
		irradianceN  = blended;
	}
	vec3 iblDiff = (sampleIrradiance(irradianceN, irrIdx) * albedo);

	// Split ambient components
	vec3 ambientDiffuse  = kD * (iblDiff * aoTerm);
	float specAO         = SpecAO_Conservative(aoTerm, NdotV, rough);
	vec3 ambientSpecular = iblSpec * specAO;

	// Fake sky visibility term
	float skyFacing    = clamp(N.y * 0.5 + 0.5, 0.0, 1.0);
	float skyOcclusion = mix(0.5, 1.0, skyFacing);
	ambientDiffuse    *= skyOcclusion;

	vec3 ambient = ambientDiffuse + ambientSpecular;

	vec3 color   = direct + localLightColor + ambient + emissive;
	outFragColor = vec4(color, 1.0);
}
