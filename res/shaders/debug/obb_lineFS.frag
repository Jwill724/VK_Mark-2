#version 450

#extension GL_ARB_separate_shader_objects : require
#extension GL_GOOGLE_include_directive : require

layout(location = 0) out vec4 outColor;

void main()
{
	outColor = vec4(1.0f, 0.0f, 0.0f, 1.0f); // red debug color
}
