#version 450

#extension GL_ARB_separate_shader_objects : require
#extension GL_GOOGLE_include_directive : require

#include "../include/common.glsl"

layout(location = 0) in vec3 inViewNormal;
layout(location = 1) in vec4 inCurrClip;
layout(location = 2) in vec4 inPrevClip;
layout(location = 3) in vec2 inUV;
layout(location = 4) flat in uint inTemporalValidation;
layout(location = 5) flat in uint inMaterialID;
layout(location = 6) flat in uint inPackedID;

layout(location = 0) out uvec2 outVisibility;
layout(location = 1) out vec4 outViewSpaceNormal;
layout(location = 2) out vec2 outVelocity;

vec2 computeVelocityUV(vec2 viewport)
{
	if (inTemporalValidation != 1u) return vec2(0.0);
	if (inCurrClip.w <= 0.0 || inPrevClip.w <= 0.0) return vec2(0.0);

	vec2 currNdc = inCurrClip.xy / inCurrClip.w;
	vec2 prevNdc = inPrevClip.xy / inPrevClip.w;

	vec2 velocityUV = (currNdc - prevNdc) * 0.5;
	vec2 viewportSz = max(viewport, vec2(1.0));
	vec2 velocityPx = clamp(velocityUV * viewportSz, vec2(-256.0), vec2(256.0));
	return velocityPx / viewportSz;
}

void main()
{
	Material mat = getMaterialBuffer().materials[inMaterialID];
	SceneData scene = getSceneData();
	float alpha = SampleTextureBias(mat.albedoID, inUV, scene.viewportSize.w).a * mat.colorFactor.a;
	if (alpha < mat.alphaCutoff) discard;

	outVisibility = uvec2(inPackedID, uint(gl_PrimitiveID));

	outViewSpaceNormal = vec4(normalize(inViewNormal) * 0.5 + 0.5, 1.0);
	outVelocity = computeVelocityUV(scene.viewportSize.xy);
}
