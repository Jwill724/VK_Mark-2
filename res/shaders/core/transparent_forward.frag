#version 450

#extension GL_GOOGLE_include_directive    : require
#extension GL_ARB_separate_shader_objects : require

#include "../include/common.glsl"
#include "../include/clustered.glsl"
#include "../include/pbr.glsl"
#include "../include/shading_functions.glsl"
#include "../include/lighting.glsl"

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec4 inViewPos;
layout(location = 4) in vec3 inNormal;
layout(location = 5) in vec3 inTangent;
layout(location = 6) in float inTangentW;
layout(location = 7) flat in uint inMaterialID;
layout(location = 8) flat in uint inBHasNormalMap;

layout(location = 0) out vec4  outAccum;
layout(location = 1) out float outReveal;

layout(push_constant) uniform ForwardPush
{
	uint diffuseID;
	uint specularID;
	uint brdfID;
	float oitDepthScale;
} pc;

void main()
{
	Material mat    = getMaterialBuffer().materials[inMaterialID];
	SceneData scene = getSceneData();
	const vec4 base = SampleTextureBias(mat.albedoID, inUV, scene.viewportSize.w) * mat.colorFactor;
	float alpha     = base.a;
	vec3  albedo    = inColor * base.rgb;

	ClusteredData clusteredData = getClusteredData();
	DebugToggles debug          = getDebugToggles();

	float viewDepth = -inViewPos.z;

	const vec3 geometricNormalWS = normalize(inNormal);
	vec3 N = geometricNormalWS;

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

	// Only need g and b
	vec3 metalRough = SampleTextureBias(mat.metalRoughnessID, inUV, scene.viewportSize.w).rgb;
	vec3 emissT     = SampleTextureBias(mat.emissiveID,       inUV, scene.viewportSize.w).rgb;

	float rough  = metalRough.g * mat.metalRoughFactors.y;
	float metal  = metalRough.b * mat.metalRoughFactors.x;

	vec3 emissive = emissT * (mat.emissiveColor * mat.emissiveStrength);
	float lum     = max(max(emissive.r, emissive.g), emissive.b);

	// No bloom so hack it a bit
	// Boost only bright parts
	float boost   = smoothstep(1.0, 10.0, lum);
	emissive     *= mix(1.0, 3.0, boost);

	vec3 sunColor = scene.sunlightColor.rgb * scene.sunlightColor.a;
	vec3 V        = normalize(scene.cameraPos.xyz - inWorldPos);
	vec3 L        = normalize(scene.sunlightDirection.xyz);
	vec3 H        = normalize(V + L);

	float NdotV = max(dot(N, V), 0.0);
	float NdotL = max(dot(N, L), 0.0);
	float LdotH = max(dot(L, H), 0.0);

	vec3 F0 = mix(vec3(0.04), albedo, metal);
	vec3 F  = F_SchlickRoughness(F0, NdotV, rough);
	vec3 kD = (1.0 - F) * (1.0 - metal);

	// Disney/Frostbite direct lighting
	vec3 diff = DisneyDiffuse(albedo, rough, NdotV, NdotL, LdotH);
	vec3 spec = BRDF_Specular(NdotV, NdotL, N, V, H, F0, rough);

	vec2 brdf = SampleTexture(pc.brdfID, vec2(NdotV, rough)).rg;
	vec3 multiScatterComp = MultiScatterEnergyComp(F0, brdf);
	spec *= multiScatterComp;

	// Sun direct — no shadow for transparents
	vec3 direct = (diff + spec) * sunColor * NdotL;

	// Cluster local lights
	vec3 localLightColor = vec3(0.0);
	if (debug.activeLightCount > 0u) {
		LightBuffer       lightBuf           = getLightBuffer();
		VisibleLightCount visibleCountBuf    = getVisibleLightCountBuffer();
		VisibleLightIDs   visibleIDsBuf      = getVisibleLightIDsBuffer();
		ClusterCounts     countsBuf          = getClusterCountsBuffer();
		ClusterOffsets    offsetsBuf         = getClusterOffsetsBuffer();
		ClusterLightIDs   clusterLightIDsBuf = getClusterLightIDsBuffer();

		ClusterGrid fragGrid = computeClusterGrid(
			gl_FragCoord.xy, viewDepth,
			uvec2(scene.viewportSize.xy),
			clusteredData.tileSizeX, clusteredData.tileSizeY,
			clusteredData.tileCountX, clusteredData.tileCountY,
			clusteredData.zSlices,
			scene.cameraClips.x, scene.cameraClips.y
		);

		uint count  = min(countsBuf.counts[fragGrid.clusterIndex], clusteredData.maxLightsPerCluster);
		uint offset = offsetsBuf.offsets[fragGrid.clusterIndex];

		for (uint i = 0u; i < count; ++i) {
			LocalLight light = lightBuf.lights[clusterLightIDsBuf.lightIDs[offset + i]];
			float unusedNdotL = 0.0;

			if (light.lightType == LIGHT_TYPE_POINT) {
				localLightColor += evaluatePointLight(
					light, inWorldPos, scene.cameraPos.xyz,
					N, V, NdotV, albedo, F0, rough, unusedNdotL);
			}
			else if (light.lightType == LIGHT_TYPE_SPOT) {
				const bool isFlashLight = (light.flags & LIGHT_FLAG_FLASHLIGHT) != 0u;
				const bool isFlashLightOff = (light.flags & LIGHT_FLAG_FLASHLIGHT_OFF) != 0u;

				if (isFlashLight && isFlashLightOff) continue;

				localLightColor += evaluateSpotLight(
					light, inWorldPos, scene.cameraPos.xyz,
					N, V, NdotV, albedo, F0, rough, unusedNdotL);
			}
		}
	}

	// IBL
	vec3 iblSpec = sampleSpecIBL(V, N, rough, F0, brdf, pc.specularID);
	iblSpec *= multiScatterComp;

	vec3 iblDiff = sampleIrradiance(N, pc.diffuseID) * albedo;

	vec3 ambientDiffuse  = kD * iblDiff;
	vec3 ambientSpecular = iblSpec;
	vec3 ambient         = ambientDiffuse + ambientSpecular;

	vec3 color = direct + localLightColor + ambient + emissive;

	float z = viewDepth;
	// Depth scale == 400.0 default
	float w = alpha * clamp(0.03 / (1e-5 + pow(z / pc.oitDepthScale, 4.0)), 1e-2, 3e3);

	outAccum  = vec4(color * alpha, alpha) * w;
	outReveal = alpha;
}
