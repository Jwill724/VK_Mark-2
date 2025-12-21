#version 450

#extension GL_ARB_separate_shader_objects : require
#extension GL_GOOGLE_include_directive : require

#include "../include/set_bindings.glsl"
#include "../include/gpu_scene_structures.glsl"

layout(location = 0) in vec3 inViewNormal;
layout(location = 1) in vec4 inCurrClipPos;
layout(location = 2) in vec4 inPrevClipPos;
layout(location = 4) flat in uint inTemporalValidation;

layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec2 outVelocity;

layout(set = FRAME_SET, binding = FRAME_BINDING_SCENE) uniform SceneUBO {
	SceneData scene;
};

const float kMinW = 1e-6;

vec2 computeVelocityUV(vec4 currClipPos, vec4 prevClipPos)
{
	if (inTemporalValidation != 1u) {
		return vec2(0.0);
	}

	float currW = currClipPos.w;
	float prevW = prevClipPos.w;

	// Reject behind camera / near w=0
	if (currW <= kMinW || prevW <= kMinW) {
		return vec2(0.0);
	}

	vec2 currNdc = currClipPos.xy / currW; // [-1, 1]
	vec2 prevNdc = prevClipPos.xy / prevW; // [-1, 1]

	vec2 velocityUV = (currNdc - prevNdc) * 0.5; // NDC -> UV

	// Pixel-space clamp
	vec2 viewportSize = max(scene.viewportSize.xy, vec2(1.0));
	vec2 velocityPx = velocityUV * viewportSize;

	float maxVelocityPx = 256.0;
	velocityPx = clamp(velocityPx, vec2(-maxVelocityPx), vec2(maxVelocityPx));

	return velocityPx / viewportSize;
}

void main() {
	outNormal = vec4(normalize(inViewNormal) * 0.5 + 0.5, 1.0); // [-1,1] -> [0,1]

	outVelocity = computeVelocityUV(inCurrClipPos, inPrevClipPos);
}