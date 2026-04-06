#version 450

#extension GL_ARB_separate_shader_objects : require
#extension GL_GOOGLE_include_directive : require

#include "../include/common.glsl"

layout(location = 0) in vec3 inViewNormal;
layout(location = 1) in vec2 inCurrNdc;
layout(location = 2) in vec2 inPrevNdc;
layout(location = 3) in vec2 inUV;
layout(location = 4) flat in uint inTemporalValidation;
layout(location = 5) flat in uint inMaterialID;

layout(location = 0) out vec4 outViewSpaceNormal;
layout(location = 1) out vec2 outVelocity;

vec2 computeVelocityUV(const vec2 viewport)
{
	if (inTemporalValidation != 1u) return vec2(0.0);

	vec2 velocityUV = (inCurrNdc - inPrevNdc) * 0.5;

	vec2 viewportSize = max(viewport.xy, vec2(1.0));
	vec2 velocityPx   = velocityUV * viewportSize;

	const float maxVelocityPx = 256.0;
	velocityPx = clamp(velocityPx, vec2(-maxVelocityPx), vec2(maxVelocityPx));

	return velocityPx / viewportSize;
}

void main() {
	Material mat = getMaterialBuffer().materials[inMaterialID];
	float alpha = SampleTexture(mat.albedoID, inUV).a * mat.colorFactor.a;
	if (alpha < mat.alphaCutoff) discard;

	outViewSpaceNormal = vec4(normalize(inViewNormal) * 0.5 + 0.5, 1.0); // [-1,1] -> [0,1]

	SceneData scene = getSceneData();
	outVelocity = computeVelocityUV(scene.viewportSize.xy);
}
