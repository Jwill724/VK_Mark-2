#version 450

#extension GL_ARB_shader_draw_parameters : require
#extension GL_ARB_separate_shader_objects : require

layout(location = 0) in vec3 inViewNormal;

layout(location = 0) out vec4 outNormal;

void main() {
	vec3 n = normalize(inViewNormal);
	outNormal = vec4(n * 0.5 + 0.5, 1.0); // [-1,1] -> [0,1]
}