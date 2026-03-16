#version 450

#extension GL_ARB_separate_shader_objects : require
#extension GL_GOOGLE_include_directive : require

#include "../include/set_bindings.glsl"
#include "../include/gpu_scene_structures.glsl"

layout(set = FRAME_SET, binding = FRAME_BINDING_SCENE) uniform SceneUBO {
	SceneData scene;
};

layout(buffer_reference, scalar) readonly buffer OBBLineBuffer
{
	vec3 vertices[];
};

layout(push_constant) uniform PushConstants
{
	OBBLineBuffer vertBuffer;
	uint pad0[2];
} pc;

void main()
{
	vec3 pos = pc.vertBuffer.vertices[gl_VertexIndex];

	gl_Position = scene.viewProj * vec4(pos, 1.0f);
}