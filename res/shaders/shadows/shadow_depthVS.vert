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
	uint packedID       = getDrawInstanceIDsBuffer().ids[gl_InstanceIndex];
	uint instanceID     = visInstanceID(packedID);
	InstanceInput inst  = getInstanceInputBuffer().instanceInputs[instanceID];

	Vertex vtx = getVertexBuffer().vertices[gl_VertexIndex];

	mat4 model = getTransformBuffer().transforms[inst.transformID];

	vec4 worldPos = model * vec4(vtx.position, 1.0);

	gl_Position = pc.viewproj * worldPos;
}
