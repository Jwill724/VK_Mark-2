#ifndef LIGHTING_GLSL
#define LIGHTING_GLSL

struct Surface
{
	vec3  worldPos;
	vec3  N;
	vec3  V;
	vec3  diffuseAlbedo;
	vec3  F0;
	vec3  kD;
	vec3  multiScatter;
	vec2  brdf;
	float rough;
	float NdotV;

	uint  shadingModel;
	float clearcoat;
	float clearcoatRough;
	vec3  sheenColor;
	float sheenRough;
	float diffuseTrans;
};

struct LightSample
{
	vec3  diffuse;
	vec3  specular;
	float NdotL;
	float transNdotL;
};

Surface makeSurface(
	vec3 worldPos, vec3 N, vec3 cameraPosWS,
	vec3 albedo, float metal, float rough, Material mat, uint brdfID)
{
	Surface s;
	s.worldPos      = worldPos;
	s.N             = N;
	s.V             = normalize(cameraPosWS - worldPos);
	s.NdotV         = max(dot(N, s.V), 0.0);
	s.rough         = rough;

	float dielF0    = DielectricF0(mat.ior, mat.specularFactor);
	s.F0            = mix(vec3(dielF0), albedo, metal);
	s.diffuseAlbedo = albedo * (1.0 - metal);
	s.brdf          = SampleTexture(brdfID, vec2(s.NdotV, rough)).rg;
	s.multiScatter  = MultiScatterEnergyComp(s.F0, s.brdf);
	s.kD            = 1.0 - F_SchlickRoughness(s.F0, s.NdotV, rough);

	s.shadingModel   = mat.shadingModel;
	s.clearcoat      = mat.clearcoatFactor;
	s.clearcoatRough = max(mat.clearcoatRough, 0.03);
	s.sheenColor     = mat.sheenColor;
	s.sheenRough     = max(mat.sheenRough, 0.07);
	s.diffuseTrans   = mat.diffuseTransFactor;
	return s;
}

float V_Kelemen(float LdotH)
{
	return 0.25 / max(LdotH * LdotH, 1e-4);
}

float D_Charlie(float NdotH, float roughness)
{
	float invAlpha = 1.0 / max(roughness, 1e-3);
	float cos2h    = NdotH * NdotH;
	float sin2h    = max(1.0 - cos2h, 1e-4);
	return (2.0 + invAlpha) * pow(sin2h, invAlpha * 0.5) / (2.0 * PI);
}

float V_Ashikhmin(float NdotV, float NdotL)
{
	return clamp(1.0 / (4.0 * (NdotL + NdotV - NdotL * NdotV)), 0.0, 1.0);
}

vec3 BRDF_Clearcoat(float NdotL, vec3 N, vec3 V, vec3 H, vec3 L,
					float ccStrength, float ccRough, out float attenuation)
{
	float NdotH = saturate(dot(N, H));
	float LdotH = saturate(dot(L, H));

	float D  = D_GGX(N, H, ccRough);
	float Vv = V_Kelemen(LdotH);
	float F  = (0.04 + 0.96 * pow(1.0 - LdotH, 5.0)) * ccStrength;

	attenuation = 1.0 - F;
	return vec3(D * Vv * F);
}

vec3 BRDF_Sheen(float NdotV, float NdotL, vec3 N, vec3 H, vec3 sheenColor, float sheenRough)
{
	float NdotH = saturate(dot(N, H));
	return sheenColor * (D_Charlie(NdotH, sheenRough) * V_Ashikhmin(NdotV, NdotL));
}

LightSample evaluateDirectional(Surface s, vec3 L, vec3 radiance)
{
	LightSample r;
	r.diffuse    = vec3(0.0);
	r.specular   = vec3(0.0);
	r.NdotL      = max(dot(s.N, L), 0.0);
	r.transNdotL = 0.0;

	vec3  H     = normalize(s.V + L);
	float LdotH = max(dot(L, H), 0.0);

	if (s.diffuseTrans > 0.0)
	{
		r.transNdotL = max(dot(-s.N, L), 0.0);
		if (r.transNdotL > 0.0)
		{
			r.diffuse += DisneyDiffuse(s.diffuseAlbedo, s.rough, s.NdotV, r.transNdotL, LdotH)
					   * radiance * r.transNdotL * s.diffuseTrans;
		}
	}

	if (r.NdotL <= 0.0) return r;

	vec3 e = radiance * r.NdotL;

	r.diffuse  += DisneyDiffuse(s.diffuseAlbedo, s.rough, s.NdotV, r.NdotL, LdotH) * e;
	r.specular  = BRDF_Specular(s.NdotV, r.NdotL, s.N, s.V, H, s.F0, s.rough) * s.multiScatter * e;

	if (s.shadingModel == SHADING_MODEL_CLEARCOAT && s.clearcoat > 0.0)
	{
		float ccAtten;
		vec3 cc = BRDF_Clearcoat(r.NdotL, s.N, s.V, H, L, s.clearcoat, s.clearcoatRough, ccAtten);
		r.diffuse  *= ccAtten;
		r.specular *= ccAtten;
		r.specular += cc * e;
	}
	else if (s.shadingModel == SHADING_MODEL_SHEEN)
	{
		r.specular += BRDF_Sheen(s.NdotV, r.NdotL, s.N, H, s.sheenColor, s.sheenRough) * e;
	}

	return r;
}

vec4 sampleReflection(
	sampler2D reflectTex, sampler2D depthTex,
	vec2 uv, float centerViewZ, vec2 halfTexel,
	float depthLinearizeMult, float depthLinearizeAdd)
{
	vec2 f    = fract(uv / halfTexel - 0.5);
	vec2 base = (floor(uv / halfTexel - 0.5) + 0.5) * halfTexel;

	vec4  sum  = vec4(0.0);
	float wSum = 0.0;

	for (int i = 0; i < 4; ++i)
	{
		vec2 o     = vec2(float(i & 1), float(i >> 1));
		vec2 tapUV = clamp(base + o * halfTexel, halfTexel * 0.5, 1.0 - halfTexel * 0.5);

		float bilinear = mix(1.0 - f.x, f.x, o.x) * mix(1.0 - f.y, f.y, o.y);

		float rawDepth = textureLod(depthTex, tapUV, 0.0).r;
		if (rawDepth <= 0.0) continue;

		float z = screenToViewDepth(rawDepth, depthLinearizeMult, depthLinearizeAdd);
		float w = bilinear / (1.0 + abs(z - centerViewZ) * 8.0);

		sum  += textureLod(reflectTex, tapUV, 0.0) * w;
		wSum += w;
	}

	return wSum > 0.0 ? sum / wSum : vec4(0.0);
}

vec4 SampleReflectionBilateral(
	SceneData scene,
	vec2 uv,
	vec2 reflectHalfTexel,
	float centerViewZ,
	sampler2D depthTex,
	sampler2D reflectDenoised)
{
	vec2  hp   = uv / reflectHalfTexel - 0.5;
	ivec2 base = ivec2(floor(hp));
	vec2  f    = hp - vec2(base);

	float bw[4]  = float[4]((1.0-f.x)*(1.0-f.y), f.x*(1.0-f.y),
							(1.0-f.x)*f.y,       f.x*f.y);
	ivec2 off[4] = ivec2[4](ivec2(0,0), ivec2(1,0), ivec2(0,1), ivec2(1,1));

	ivec2 halfMax = textureSize(reflectDenoised, 0) - 1;
	vec2  vpSize  = scene.renderExtentSize.xy;

	vec4  sum  = vec4(0.0);
	float wsum = 0.0;

	for (int i = 0; i < 4; ++i)
	{
		ivec2 h    = clamp(base + off[i], ivec2(0), halfMax);
		vec2  huv  = (vec2(h) + 0.5) * reflectHalfTexel;
		ivec2 full = clamp(ivec2(huv * vpSize), ivec2(0), ivec2(vpSize) - 1);

		float z = screenToViewDepth(texelFetch(depthTex, full, 0).r,
					scene.depthLinearizeMult, scene.depthLinearizeAdd);

		float w = bw[i] * exp(-abs(z - centerViewZ) / max(centerViewZ * 0.02, 1e-4));

		sum  += texelFetch(reflectDenoised, h, 0) * w;
		wsum += w;
	}

	return wsum > 1e-5
		? sum / wsum
		: texelFetch(reflectDenoised, clamp(base, ivec2(0), halfMax), 0);
}

float specularAAAnalytic(float roughness, vec3 dndx, vec3 dndy)
{
	float normalVariance = dot(dndx, dndx) + dot(dndy, dndy);
	float filteredRoughness2 = roughness * roughness + normalVariance;
	return clamp(sqrt(filteredRoughness2), 0.04, 1.0);
}

float MicroShadowVisibility(float NdotL, float occlusion)
{
	float ao = clamp(occlusion, 0.0, 1.0);
	float ao2 = ao * ao;
	return saturate(NdotL + 2 * ao2 - 1);
}

float bentConeVisibility(vec3 bentNormalWS, vec3 dirWS, float ao)
{
	float coneCos = sqrt(saturate(1.0 - ao));
	float d       = dot(bentNormalWS, dirWS);

	float t = smoothstep(coneCos - 0.5, coneCos, d);
	return mix(ao * ao, 1.0, t);
}

float RefractionLod(float rough, float ior, float mipCount)
{
	float r = rough * clamp(ior * 2.0 - 2.0, 0.0, 1.0);
	return r * (mipCount - 1.0);
}

vec3 EvaluateTransmission(
	Surface s, mat4 viewProj, sampler2D refractionColor, float mipCount,
	float ior, float thickness, vec3 modelScale,
	vec3 attenColor, float attenDist)
{
	vec3 refractDir = refract(-s.V, s.N, 1.0 / max(ior, 1.0));
	if (dot(refractDir, refractDir) < 1e-6) return vec3(0.0);

	vec3 refractionRay = normalize(refractDir) * thickness * modelScale;
	vec3 exitPos       = s.worldPos + refractionRay;

	vec4 clip = viewProj * vec4(exitPos, 1.0);
	if (clip.w <= 0.0) return vec3(0.0);

	vec4 entryClip = viewProj * vec4(s.worldPos, 1.0);
	vec2 entryUV   = entryClip.xy / entryClip.w * 0.5 + 0.5;
	vec2 exitUV    = clip.xy / clip.w * 0.5 + 0.5;

	vec2 offset = exitUV - entryUV;
	float offLen = length(offset);
	if (offLen > MAX_REFRACT_OFFSET)
		offset *= MAX_REFRACT_OFFSET / offLen;

	vec2 uv = clamp(entryUV + offset, vec2(0.0), vec2(1.0));

	float iorFade = clamp(ior * 2.0 - 2.0, 0.0, 1.0);
	float lod     = s.rough * iorFade * (mipCount - 1.0);

	vec3 behind = textureLod(refractionColor, uv, lod).rgb;

	float thicknessWS = length(refractionRay);
	vec3 transmittance = vec3(1.0);
	if (attenDist > 0.0 && thicknessWS > 0.0)
		transmittance = exp(log(max(attenColor, vec3(1e-4))) * (thicknessWS / attenDist));

	return behind * transmittance * s.diffuseAlbedo * s.kD;
}

vec3 sampleIrradiance(vec3 N, vec3 shCoeffs[9])
{
	N.y = -N.y;

	const float c1 = 0.429043, c2 = 0.511664, c3 = 0.743125, c4 = 0.886227, c5 = 0.247708;

	vec3 L00  = shCoeffs[0];
	vec3 L1m1 = shCoeffs[1];
	vec3 L10  = shCoeffs[2];
	vec3 L11  = shCoeffs[3];
	vec3 L2m2 = shCoeffs[4];
	vec3 L2m1 = shCoeffs[5];
	vec3 L20  = shCoeffs[6];
	vec3 L21  = shCoeffs[7];
	vec3 L22  = shCoeffs[8];

	return max(vec3(0.0),
		  c1 * L22 * (N.x * N.x - N.y * N.y)
		+ c3 * L20 * (N.z * N.z)
		+ c4 * L00
		- c5 * L20
		+ 2.0 * c1 * (L2m2 * N.x * N.y + L21 * N.x * N.z + L2m1 * N.y * N.z)
		+ 2.0 * c2 * (L11 * N.x + L1m1 * N.y + L10 * N.z));
}

vec3 sampleSpecIBL(vec3 V, vec3 N, float roughness, vec3 F0, vec2 brdf, uint specIdx)
{
	vec3 R = reflect(-V, N);
	R.y = -R.y;

	int levels = SampleCubeQueryLevels(specIdx);
	float lod = clamp(roughness * float(levels - 1), 0.0, float(levels - 1));
	vec3 prefiltered = SampleCubeLod(specIdx, R, lod).rgb;

	return prefiltered * (F0 * brdf.x + brdf.y);
}

void evaluateAmbient(out vec3 ambientDiffuse, out vec3 ambientSpecular,
	Surface s, vec3 irradiance, vec3 giRadiance,
	float shWeight, float specOcclusion, float giSpecBlend,
	uint specularID, vec4 rtSpec)
{
	vec3 skyDiffuse = (1.0 / PI) * irradiance * shWeight;

	ambientDiffuse = s.kD * s.diffuseAlbedo * (skyDiffuse + giRadiance);

	vec3 dfg = s.F0 * s.brdf.x + s.brdf.y;

	vec3 iblSpec  = sampleSpecIBL(s.V, s.N, s.rough, s.F0, s.brdf, specularID) * s.multiScatter;
	vec3 ssgiSpec = (giRadiance + skyDiffuse) * dfg * s.multiScatter;

	vec3 screenSpec = mix(iblSpec, ssgiSpec, giSpecBlend) * specOcclusion;
	vec3 tracedSpec = rtSpec.rgb * dfg * s.multiScatter;

	ambientSpecular = mix(screenSpec, tracedSpec, rtSpec.a);
}

// Transparent / forward path.
void evaluateAmbient(out vec3 ambientDiffuse, out vec3 ambientSpecular,
	Surface s, vec3 irradiance, vec3 giRadiance,
	float shWeight, float specOcclusion, uint specularID)
{
	evaluateAmbient(ambientDiffuse, ambientSpecular, s,
					irradiance, giRadiance, shWeight, specOcclusion,
					0.0, specularID, vec4(0.0));
}

// =============================================
// === CLUSTER SHADING FUNCTIONS DEFINITIONS ===

float radiusAttenuation(float distanceToLight, float radius)
{
	float x = distanceToLight / max(radius, 1e-6);
	float t = clamp(1.0 - x * x, 0.0, 1.0);
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

vec3 ClosestPointOnSegment(vec3 p, vec3 a, vec3 b)
{
	vec3 ab = b - a;
	float t = saturate(dot(p - a, ab) / max(dot(ab, ab), 1e-6));
	return a + ab * t;
}

vec3 ClosestPointOnRay(vec3 origin, vec3 dir, vec3 a, vec3 b)
{
	vec3 ab   = b - a;
	vec3 ao   = a - origin;
	float abD = dot(ab, dir);
	float ab2 = dot(ab, ab);
	float aoD = dot(ao, dir);

	float denom = ab2 - abD * abD;
	if (abs(denom) < 1e-6) return a;

	float t = saturate((abD * aoD - dot(ao, ab)) / denom);
	return a + ab * t;
}

LightSample evaluateAreaLight(
	Surface s, vec3 lightPosWS, vec3 lightAxisWS,
	float sourceRadius, float sourceLength, vec3 radiance)
{
	LightSample r;
	r.diffuse  = vec3(0.0);
	r.specular = vec3(0.0);
	r.NdotL    = 0.0;

	vec3 p0 = lightPosWS;
	vec3 p1 = lightPosWS;
	if (sourceLength > 0.0)
	{
		vec3 halfLight = lightAxisWS * (sourceLength * 0.5);
		p0 = lightPosWS - halfLight;
		p1 = lightPosWS + halfLight;
	}

	vec3  diffusePoint = (sourceLength > 0.0)
		? ClosestPointOnSegment(s.worldPos, p0, p1)
		: lightPosWS;

	vec3  toDiffuse = diffusePoint - s.worldPos;
	float distD     = max(length(toDiffuse), 1e-6);
	vec3  Ld        = toDiffuse / distD;

	r.NdotL = max(dot(s.N, Ld), 0.0);
	if (r.NdotL <= 0.0) return r;

	vec3 R = reflect(-s.V, s.N);

	vec3 axisPoint = (sourceLength > 0.0)
		? ClosestPointOnRay(s.worldPos, R, p0, p1)
		: lightPosWS;

	vec3  toAxis      = axisPoint - s.worldPos;
	vec3  centerToRay = dot(toAxis, R) * R - toAxis;
	float ctrLen      = max(length(centerToRay), 1e-6);
	vec3  closest     = toAxis + centerToRay * saturate(sourceRadius / ctrLen);

	float distS = max(length(closest), 1e-6);
	vec3  Ls    = closest / distS;

	float alpha      = LinearRough(s.rough);
	float alphaPrime = saturate(alpha + sourceRadius / (2.0 * distS));

	float energy = alpha / max(alphaPrime, 1e-6);
	energy *= energy;

	if (sourceLength > 0.0)
		energy *= alpha / max(saturate(alpha + sourceLength / (2.0 * distS)), 1e-6);

	float NdotLs = max(dot(s.N, Ls), 0.0);
	vec3  e       = radiance * r.NdotL;

	float LdotH = max(dot(Ld, normalize(s.V + Ld)), 0.0);
	r.diffuse = DisneyDiffuse(s.diffuseAlbedo, s.rough, s.NdotV, r.NdotL, LdotH) * e;

	if (NdotLs > 0.0)
	{
		vec3 H = normalize(s.V + Ls);
		r.specular = BRDF_Specular(s.NdotV, NdotLs, s.N, s.V, H, s.F0, sqrt(alphaPrime))
				   * s.multiScatter * energy * radiance * NdotLs;
	}

	return r;
}

LightSample evaluateLocalLight(LocalLight light, Surface s, vec3 cameraPosWS)
{
	LightSample r;
	r.diffuse  = vec3(0.0);
	r.specular = vec3(0.0);
	r.NdotL    = 0.0;

	vec3  toLight = light.position - s.worldPos;
	float dist    = length(toLight);
	if (dist >= light.radius) return r;

	vec3  L         = toLight / max(dist, 1e-6);
	float attenDist = max(dist, light.sourceRadius);

	float atten = radiusAttenuation(dist, light.radius)
				* inverseSquareAttenuation(attenDist)
				* lightCameraFade(light, cameraPosWS);

	if ((light.flags & LIGHT_FLAG_SPOT) != 0u)
		atten *= spotConeFactor(light.direction, L, light.innerCos, light.outerCos);

	if (atten <= 0.0) return r;

	vec3 radiance = light.color * light.intensity * atten;

	if (light.sourceRadius > 0.0 || light.sourceLength > 0.0)
		return evaluateAreaLight(s, light.position, light.direction,
			light.sourceRadius, light.sourceLength, radiance);

	return evaluateDirectional(s, L, radiance);
}

#endif
