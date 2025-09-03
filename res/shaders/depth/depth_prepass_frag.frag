#version 450

#extension GL_ARB_shader_draw_parameters : require
#extension GL_ARB_separate_shader_objects : require

layout(location = 0) in vec3 inNormal;

layout(location = 0) out vec4 outNormal;

void main() {
	outNormal = vec4(normalize(inNormal) * 0.5 + 0.5, 1.0);
}