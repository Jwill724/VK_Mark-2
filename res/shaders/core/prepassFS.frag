#version 450

#extension GL_ARB_separate_shader_objects : require
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier    : require

#include "../include/set_bindings.glsl"
#include "../include/gpu_scene_structures.glsl"

layout(location = 0) in vec3 inViewNormal;
layout(location = 1) in vec2 inCurrNdc;
layout(location = 2) in vec2 inPrevNdc;
layout(location = 3) in vec2 inUV;
layout(location = 4) flat in uint inTemporalValidation;
layout(location = 5) flat in uint inMaterialID;

layout(location = 0) out vec4 outNormal;
layout(location = 1) out vec2 outVelocity;

layout(set = FRAME_SET, binding = FRAME_BINDING_SCENE) uniform SceneUBO {
	SceneData scene;
};

layout(set = GLOBAL_SET, binding = ADDRESS_TABLE_BINDING, scalar) readonly buffer GlobalAddressTableBuffer {
	GPUAddressTable globalAddressTable;
};

layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_COMBINED_SAMPLER) uniform sampler2D combinedSamplers[];

vec2 computeVelocityUV()
{
	if (inTemporalValidation != 1u) return vec2(0.0);

	vec2 velocityUV = (inCurrNdc - inPrevNdc) * 0.5;

	vec2 viewportSize = max(scene.viewportSize.xy, vec2(1.0));
	vec2 velocityPx = velocityUV * viewportSize;

	const float maxVelocityPx = 256.0;
	velocityPx = clamp(velocityPx, vec2(-maxVelocityPx), vec2(maxVelocityPx));

	return velocityPx / viewportSize;
}

void main() {
	Material mat = MaterialBuffer(globalAddressTable.addrs[ABT_Material]).materials[inMaterialID];
	float alpha = texture(combinedSamplers[nonuniformEXT(mat.albedoID)], inUV).a * mat.colorFactor.a;
	if (alpha < mat.alphaCutoff) discard;

	outNormal = vec4(normalize(inViewNormal) * 0.5 + 0.5, 1.0); // [-1,1] -> [0,1]

	outVelocity = computeVelocityUV();
}
