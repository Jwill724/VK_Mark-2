#version 450

#extension GL_ARB_shader_draw_parameters : require
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : require

#include "../include/common.glsl"

layout(push_constant) uniform ShadowPush {
	mat4 viewproj;
} pc;

void main()
{
	// fetch shadow caster instances
	Instance inst = getInstanceBuffer().instances[gl_InstanceIndex];

	// fetch vertex
	Vertex vtx = getVertexBuffer().vertices[gl_VertexIndex];

	// fetch transform
	mat4 model = getTransformBuffer().transforms[inst.transformID];

	vec4 worldPos = model * vec4(vtx.position, 1.0);

	gl_Position = pc.viewproj * worldPos;
}
