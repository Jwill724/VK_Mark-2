#version 450

#extension GL_GOOGLE_include_directive    : require
#extension GL_ARB_separate_shader_objects : require

#include "../include/clustered.glsl"
#include "../include/common.glsl"
#include "../include/pbr.glsl"
#include "../include/depth.glsl"
#include "../include/shadow.glsl"
#include "../include/lighting.glsl"
#include "../include/nrd_common.glsl"

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inWorldPos;
layout(location = 3) in vec4 inViewPos;
layout(location = 4) in vec3 inNormal;
layout(location = 5) in vec3 inTangent;
layout(location = 6) in float inTangentW;
layout(location = 7) flat in uint inMaterialID;
layout(location = 8) flat in uint inBHasNormalMap;
layout(location = 9) in vec4 inCurrClip;
layout(location = 10) in vec4 inPrevClip;

layout(location = 0) out vec4  outAccum;
layout(location = 1) out float outReveal;
layout(location = 2) out vec2 outVelocity;

layout(set = PUSH_SET, binding = PUSH_BINDING_READ_1) uniform sampler2D rtShadowDenoised;

layout(push_constant) uniform ForwardPush
{
	vec2 halfTexel;
	uint specularID;
	uint brdfID;
	float oitDepthScale;
	float bounceFeedback;
	float giIntensity;
	uint flashlightShadowMapID;
	uint flashlightCookieTexID;
} pc;

void main()
{
	Material  mat     = getMaterialBuffer().materials[inMaterialID];
	SceneData scene   = getSceneData();
	float mipBias     = scene.taaMipParams.x;
	const vec4 base   = SampleTextureBias(mat.albedoID, inUV, mipBias) * mat.colorFactor;
	float      alpha  = base.a;
	vec3       albedo = inColor * base.rgb;

	uint visibleLightCount = getVisibleLightCountBuffer().count;

	ClusteredData clusteredData = getClusteredData();
	DebugToggles  debug         = getDebugToggles();

	float viewDepth = -inViewPos.z;
	vec2  fragCoord = gl_FragCoord.xy;

	const float faceSign          = gl_FrontFacing ? 1.0 : -1.0;
	const vec3  geometricNormalWS = normalize(inNormal) * faceSign;
	vec3 N = geometricNormalWS;

	if (inBHasNormalMap == 1u)
	{
		vec2 nxy = SampleTextureBias(mat.normalID, inUV, mipBias).rg * 2.0 - 1.0;
		nxy *= mat.normalScale;

		float nz = sqrt(max(1.0 - dot(nxy, nxy), 0.0));
		vec3  normalTS = vec3(nxy, nz);

		vec3 T   = normalize(inTangent - geometricNormalWS * dot(geometricNormalWS, inTangent));
		vec3 B   = cross(geometricNormalWS, T) * inTangentW * faceSign;
		mat3 tbn = mat3(T, B, geometricNormalWS);

		N = normalize(tbn * normalTS);

	}

	vec3  metalRough = SampleTextureBias(mat.metalRoughnessID, inUV, mipBias).rgb;
	vec3  emissT     = SampleTextureBias(mat.emissiveID,       inUV, mipBias).rgb;

	float rough = metalRough.g * mat.metalRoughFactors.y;
	float metal = metalRough.b * mat.metalRoughFactors.x;

	vec3 emissive = emissT * (mat.emissiveColor * (mat.emissiveStrength * EMISSIVE_STRENGTH_BOOST));

	Surface surf = makeSurface(inWorldPos, N, scene.cameraPos.xyz, albedo, metal, rough, mat, pc.brdfID);

	vec3 sunColor = scene.sunlightColor.rgb * scene.sunlightColor.a;
	vec3 L        = normalize(scene.sunlightDirection.xyz);

	float sunShadow = 1.0;

	if (DBG(enableShadows))
	{
		if (debug.sunShadowFilter == SUN_SHADOW_FILTER_RT_SOFT)
		{
			ivec2 shadowPixel = ivec2(gl_FragCoord.xy);

			sunShadow = SIGMA_BackEnd_UnpackShadow(
				texelFetch(rtShadowDenoised, shadowPixel, 0).r);
		}
		else if (!DBG(csmAtlasCached))
		{
			sunShadow = sampleSunShadowCSM(
				inWorldPos,
				geometricNormalWS,
				L,
				viewDepth,
				fragCoord,
				debug.sunShadowFilter,
				false);
		}
	}

	LightSample sun = evaluateDirectional(surf, L, sunColor);

	vec3 direct = (sun.diffuse + sun.specular) * sunShadow;

	vec3 localLightColor = vec3(0.0);
	if (visibleLightCount > 0)
	{
		LightBuffer     lightBuf           = getLightBuffer();
		ClusterCounts   countsBuf          = getClusterCountsBuffer();
		ClusterOffsets  offsetsBuf         = getClusterOffsetsBuffer();
		ClusterLightIDs clusterLightIDsBuf = getClusterLightIDsBuffer();

		mat2 shadowHash = createHash(fragCoord);

		ClusterGrid fragGrid = computeClusterGrid(
			gl_FragCoord.xy, viewDepth,
			uvec2(scene.viewportSize.xy),
			clusteredData.tileSizeX, clusteredData.tileSizeY,
			clusteredData.tileCountX, clusteredData.tileCountY,
			clusteredData.zSlices,
			scene.cameraClips.x, scene.cameraClips.y);

		uint count  = min(countsBuf.counts[fragGrid.clusterIndex], clusteredData.maxLightsPerCluster);
		uint offset = offsetsBuf.offsets[fragGrid.clusterIndex];

		for (uint i = 0u; i < count; ++i)
		{
			LocalLight light = lightBuf.lights[clusterLightIDsBuf.lightIDs[offset + i]];

			const bool isFlashLight    = (light.flags & LIGHT_FLAG_FLASHLIGHT) != 0u;
			const bool isFlashLightOff = (light.flags & LIGHT_FLAG_FLASHLIGHT_OFF) != 0u;
			if (isFlashLight && isFlashLightOff) continue;

			LightSample ls = evaluateLocalLight(light, surf, scene.cameraPos.xyz);
			if (ls.NdotL <= 0.0 && ls.transNdotL <= 0.0) continue;

			float atten = 1.0;

			if (isFlashLight && (light.flags & LIGHT_FLAG_CASTS_SPOT_SHADOW) != 0u)
			{
				vec2  flashUV;
				float flashZ;
				projectToLightSpace(inWorldPos, scene.flashlightVP, flashUV, flashZ);

				bool inUV = all(greaterThanEqual(flashUV, vec2(0.0)))
						 && all(lessThanEqual(flashUV, vec2(1.0)));

				if (inUV && flashZ >= 0.0 && flashZ <= 1.0)
				{
					float angleScale = 1.0 - saturate(ls.NdotL);
					float bias       = MIN_SHADOW_BIAS * (0.25 + angleScale * 0.65);
					atten *= PCFPoissonLow(shadowHash, pc.flashlightShadowMapID,
										   flashUV, flashZ, bias, FLASHLIGHT_TEXEL_SIZE);
				}

				atten *= inUV ? pow(SampleTexture(pc.flashlightCookieTexID, flashUV).r, 2.0) : 0.0;
			}

			localLightColor += (ls.diffuse + ls.specular) * atten;
		}
	}

	uint envIndex = min(debug.activeEnvMap, MAX_ENV_SETS - 1u);
	vec3 irradiance = sampleIrradiance(N, getSHIrradianceBuffer().shIrr[envIndex].sh);

	vec3 ambientDiffuse;
	vec3 ambientSpecular;
	evaluateAmbient(ambientDiffuse, ambientSpecular, surf,
					irradiance, vec3(0.0), 1.0, 1.0, pc.specularID);

	vec3 color = direct + localLightColor + ambientDiffuse + ambientSpecular;

	float z = viewDepth;
	float w = alpha * clamp(0.03 / (1e-5 + pow(z / pc.oitDepthScale, 4.0)), 1e-2, 3e3);

	outAccum  = vec4(color * alpha + emissive, alpha) * w;
	outReveal = alpha;

	vec2 currNdc = inCurrClip.xy / inCurrClip.w;
	vec2 prevNdc = inPrevClip.xy / inPrevClip.w;
	vec2 velocity = (currNdc - prevNdc) * vec2(0.5, -0.5);

	outVelocity = velocity * (alpha * w);
}
