#version 450

#extension GL_GOOGLE_include_directive    : require
#extension GL_ARB_separate_shader_objects : require

#include "../include/clustered.glsl"
#include "../include/common.glsl"
#include "../include/depth.glsl"
#include "../include/shadow.glsl"
#include "../include/pbr.glsl"
#include "../include/shading_functions.glsl"
#include "../include/lighting.glsl"
#include "../include/debug_views.glsl"

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec4 inViewPos;
layout(location = 4) in vec3 inNormal;
layout(location = 5) in vec3 inTangent;
layout(location = 6) in float inTangentW;
layout(location = 7) flat in uint inMaterialID;
layout(location = 8) flat in uint inBHasNormalMap;

layout(location = 0) out vec4 outFragColor;

layout(set = PUSH_SET, binding = PUSH_BINDING_READ_1) uniform sampler2D aoFinal;
layout(set = PUSH_SET, binding = PUSH_BINDING_READ_2) uniform sampler2D contactShadowMask;
layout(set = PUSH_SET, binding = PUSH_BINDING_READ_3) uniform sampler2D bentNormals;

layout(push_constant) uniform ForwardPush
{
	uint diffuseID;
	uint specularID;
	uint brdfID;
	float oitDepthScale;

	uint flashlightShadowMapID;
	uint flashlightCookieTexID;
} pc;

void main()
{
	Material mat      = getMaterialBuffer().materials[inMaterialID];
	SceneData scene   = getSceneData();
	const vec4  base  = SampleTextureBias(mat.albedoID, inUV, scene.viewportSize.w) * mat.colorFactor;
	const float alpha = base.a;
	if (alpha < mat.alphaCutoff) discard;

	ClusteredData clusteredData = getClusteredData();
	DebugToggles debug          = getDebugToggles();

	vec2 screenspace_uv = gl_FragCoord.xy / vec2(scene.viewportSize.xy);

	// Right handed view on the -z
	const float viewDepth = -inViewPos.z;

	// geometry basis (world space)
	const vec3 geometricNormalWS = normalize(inNormal);
	vec3 N                       = geometricNormalWS;

	if (inBHasNormalMap == 1u)
	{
		vec3 normalTex = SampleTextureBias(mat.normalID, inUV, scene.viewportSize.w).rgb;

		vec3 T   = normalize(inTangent - geometricNormalWS * dot(geometricNormalWS, inTangent));
		vec3 B   = cross(geometricNormalWS, T) * inTangentW;
		mat3 tbn = mat3(T, B, geometricNormalWS);

		vec3 normalTS = normalTex * 2.0 - 1.0;
		normalTS.xy  *= mat.normalScale;
		normalTS      = normalize(normalTS);

		N = normalize(tbn * normalTS);
	}

	if (debug.debugView == DBG_NORMALS) RET(N * 0.5 + 0.5, 1.0);

	vec3 albedo = inColor * base.rgb;

	// Only need g and b
	vec3 metalRough = SampleTextureBias(mat.metalRoughnessID, inUV, scene.viewportSize.w).rgb;
	vec3 emissT     = SampleTextureBias(mat.emissiveID,       inUV, scene.viewportSize.w).rgb;

	float rough  = metalRough.g * mat.metalRoughFactors.y;
	float metal  = metalRough.b * mat.metalRoughFactors.x;

	float emissiveStrength = (mat.emissiveStrength * 15.0);
	vec3  emissive = emissT * (mat.emissiveColor * emissiveStrength);

	rough = specularAA(rough, N);
	metal = clamp(metal, 0.0, 1.0);

	if (debug.debugView == DBG_ALBEDO)    RET(albedo,      alpha);
	if (debug.debugView == DBG_EMISSIVE)  RET(emissive,    alpha);
	if (debug.debugView == DBG_METALLIC)  RET(vec3(metal), alpha);
	if (debug.debugView == DBG_ROUGHNESS) RET(vec3(rough), alpha);

	vec3 sunColor = scene.sunlightColor.rgb * scene.sunlightColor.a;
	vec3 V = normalize(scene.cameraPos.xyz - inWorldPos);
	vec3 L = normalize(scene.sunlightDirection.xyz);
	vec3 H = normalize(V + L);

	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	float LdotH = max(dot(L, H), 0.0);

	// Screen space ambient occlusion
	float aoTerm = 1.0;
	if (DBG(aoMode)) {
		float aoFactor = texture(aoFinal, screenspace_uv).r;
		if (debug.debugView == DBG_SSAO) RET(vec3(aoFactor), 1.0);
		aoTerm *= aoFactor;
	}

	// Screen space contact shadow
	float contactShadows = 1.0;
	if (DBG(enableSSS) && DBG(enableShadows)) {
		float sss = texture(contactShadowMask, screenspace_uv).r;
		if (debug.debugView == DBG_SS_SHADOWS) RET(vec3(sss),      1.0);
		contactShadows = sss;
	}

	vec3 F0 = mix(vec3(0.04), albedo, metal);
	vec3 F  = F_SchlickRoughness(F0, NdotV, rough);
	vec3 kD = (1.0 - F) * (1.0 - metal);

	// Disney/Frostbite direct lighting
	vec3 diff = DisneyDiffuse(albedo, rough, NdotV, NdotL, LdotH);
	vec3 spec = BRDF_Specular(NdotV, NdotL, N, V, H, F0, rough);

	// multi-scatter energy compensation
	vec2 brdf = SampleTexture(pc.brdfID, vec2(NdotV, rough)).rg;
	vec3 multiScatterComp = MultiScatterEnergyComp(F0, brdf);
	spec *= multiScatterComp;

	// cascaded shadow maps
	ShadowCSM csm    = getShadowCSM();
	float shadow     = 1.0;
	mat2  shadowHash = mat2(1.0);
	if (debug.aaMode != AA_TAA) {
		shadowHash = createHash(gl_FragCoord.xy);
	}
	else {
		shadowHash = createHashTemporal(gl_FragCoord.xy, scene.temporal.x);
	}

	const uint  cascadeCount = uint(csm.params.y);
	const uint cascadeIdx = cascadeViewDepthSplit(viewDepth, cascadeCount, csm.cascadeSplits);
	if (debug.debugView == DBG_CASCADES)
		RET(debugCascadeOverlay(albedo, cascadeIdx), alpha);

	if (DBG(enableShadows)) {
		const uint  shadowMapID  = uint(csm.params.x);
		const float texel        = csm.params.z;

		const uint nextIdx    = min(cascadeIdx + 1u, MAX_CASCADES_INDEX);

		const float radius     = csm.maxFilterRadiusTexels[cascadeIdx];
		const float nextRadius = csm.maxFilterRadiusTexels[nextIdx];

		const float curWorldTexel  = csm.cascadeWorldTexels[cascadeIdx];
		const float nextWorldTexel = csm.cascadeWorldTexels[nextIdx];

		const mat4 curCascadeVP  = csm.cascadeVP[cascadeIdx];
		const mat4 nextCascadeVP = csm.cascadeVP[nextIdx];

		float maxPlaneBiasCur = computeMaxPlaneBias(radius, curWorldTexel,
			ndcDepthPerWorld(curCascadeVP));

		float maxPlaneBiasNext = computeMaxPlaneBias(nextRadius, nextWorldTexel,
			ndcDepthPerWorld(nextCascadeVP));

		vec3 offsetVec     = computeNormalOffset(geometricNormalWS, L, curWorldTexel);
		vec3 nextOffsetVec = computeNormalOffset(geometricNormalWS, L, nextWorldTexel);
		vec3 offsetPos     = inWorldPos + offsetVec;
		vec3 nextOffsetPos = inWorldPos + nextOffsetVec;

		// transform into light space
		vec4 lightSpacePos = curCascadeVP * vec4(offsetPos, 1.0);
		vec3 projCoords    = lightSpacePos.xyz / lightSpacePos.w;
		vec2 shadowUV      = projCoords.xy * 0.5 + 0.5;           // [-1, 1] to [0, 1]
		shadowUV.y         = 1.0 - shadowUV.y;                    // Flip y orientation
		float curDepth     = projCoords.z;                        // z already in [0, 1]

		vec4 nextLightVP    = nextCascadeVP * vec4(nextOffsetPos, 1.0);
		vec3 nextProjCoords = nextLightVP.xyz / nextLightVP.w;
		vec2 nextShadowUV   = nextProjCoords.xy * 0.5 + 0.5;
		nextShadowUV.y      = 1.0 - nextShadowUV.y;
		float nextDepth     = nextProjCoords.z;

		vec4 atlas        = csm.atlasUV[cascadeIdx];
		vec2 atlasUV      = shadowUV * atlas.xy + atlas.zw;
		vec2 atlasMin     = atlas.zw;
		vec2 atlasMax     = atlas.zw + atlas.xy;

		vec4 nextAtlas    = csm.atlasUV[nextIdx];
		vec2 nextAtlasUV  = nextShadowUV * nextAtlas.xy + nextAtlas.zw;
		vec2 nextAtlasMin = nextAtlas.zw;
		vec2 nextAtlasMax = nextAtlas.zw + nextAtlas.xy;

		vec2 depthGradCur  = computeDepthGradientUV(atlasUV,     curDepth);
		vec2 depthGradNext = computeDepthGradientUV(nextAtlasUV, nextDepth);

		if (!(shadowUV.x < 0.0 || shadowUV.x > 1.0 ||
			  shadowUV.y < 0.0 || shadowUV.y > 1.0 ||
			  curDepth   < 0.0 || curDepth   > 1.0))
		{
			float sA = PCFVogel(
				shadowHash, shadowMapID, atlasUV, curDepth,
				texel, radius, atlasMin, atlasMax, depthGradCur, maxPlaneBiasCur);

			shadow = sA;

			// https://github.com/Williscool13/WillEngineV3/blob/54ea902fc64796c1b88ae63e2ad2ffb0da957b21/shaders/shadow_functions.slang#L89
			// The code for comparing view depth with splits and the blend itself copied from this.
			// Blending between cascades for smooth transitions.
			if (cascadeIdx < MAX_CASCADES_INDEX) {
				float blendEnd   = csm.cascadeSplits[cascadeIdx];
				float blendStart = blendEnd * 0.9;

				if (viewDepth >= blendStart) {
					if (!(nextShadowUV.x < 0.0 || nextShadowUV.x > 1.0 ||
						  nextShadowUV.y < 0.0 || nextShadowUV.y > 1.0 ||
						  nextDepth      < 0.0 || nextDepth      > 1.0))
					{
						float sB = PCFVogel(
							shadowHash, shadowMapID, nextAtlasUV, nextDepth,
							texel, nextRadius, nextAtlasMin, nextAtlasMax, depthGradNext, maxPlaneBiasNext);

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
	if (debug.activeLightCount > 0u) {
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
			if (light.lightType == LIGHT_TYPE_POINT) {
				float pointNdotL = 0.0;
				vec3 pointResult = evaluatePointLight(
					light, inWorldPos, scene.cameraPos.xyz, N, V, NdotV,
					albedo, F0, rough, pointNdotL);

				pointResult *= MicroShadowVisibility(pointNdotL, aoTerm);
				localLightColor += pointResult;
			}
			else if (light.lightType == LIGHT_TYPE_SPOT) {
				const bool isFlashLight = (light.flags & LIGHT_FLAG_FLASHLIGHT) != 0u;
				const bool isFlashLightOff = (light.flags & LIGHT_FLAG_FLASHLIGHT_OFF) != 0u;

				if (isFlashLight && isFlashLightOff) continue;

				float spotNdotL = 0.0;
				vec3 lightResult = evaluateSpotLight(
					light, inWorldPos, scene.cameraPos.xyz, N, V, NdotV,
					albedo, F0, rough, spotNdotL);

				lightResult *= MicroShadowVisibility(spotNdotL, aoTerm);

				bool castsShadow = (light.flags & LIGHT_FLAG_CASTS_SPOT_SHADOW) != 0u;
				// Shadow only for flashlight for now
				if (castsShadow && isFlashLight && !isFlashLightOff) {

					// Project world pos into flashlight clip
					vec4 flashlightSpacePos   = scene.flashlightVP * vec4(inWorldPos, 1.0);
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

						float shadowTerm = PCFPoissonLow(
							shadowHash,
							pc.flashlightShadowMapID,
							flashlightShadowUV,
							flashlightShadowZ,
							flashlightShadowBias,
							FLASHLIGHT_TEXEL_SIZE);

						lightResult *= shadowTerm;
					}

					// Cookie / gobo (projected spotlight mask)
					float cookieGobo = 0.0;

					// Outside projection => no cookie contribution
					if (!(flashlightShadowUV.x < 0.0 || flashlightShadowUV.x > 1.0 ||
						  flashlightShadowUV.y < 0.0 || flashlightShadowUV.y > 1.0))
					{
						float rawCookie = SampleTexture(pc.flashlightCookieTexID, flashlightShadowUV).r;
						cookieGobo      = pow(rawCookie, 2.0); // Make texture stick out more
					}

					lightResult *= cookieGobo;
				}

				localLightColor += lightResult;
			}
		}
	}

	// Direct sun light
	float microVisSun = MicroShadowVisibility(NdotL, aoTerm);
	vec3 direct = (diff + spec) * sunColor * NdotL * shadow * microVisSun * contactShadows;

	// IBL specular
	vec3 iblSpec = sampleSpecIBL(V, N, rough, F0, brdf, pc.specularID);
	iblSpec *= multiScatterComp;

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

		vec3 blended = normalize(mix(N, bentWS, bentBlend));
		irradianceN  = blended;
	}
	vec3 iblDiff = (sampleIrradiance(irradianceN, pc.diffuseID) * albedo);

	// Split ambient components
	vec3 ambientDiffuse  = kD * (iblDiff * aoTerm);
	float specAO         = SpecAO_Conservative(aoTerm, NdotV, rough);
	vec3 ambientSpecular = iblSpec * specAO;

	vec3 ambient = ambientDiffuse + ambientSpecular;

	vec3 color   = direct + localLightColor + ambient + emissive;
	outFragColor = vec4(color, 1.0);
}
