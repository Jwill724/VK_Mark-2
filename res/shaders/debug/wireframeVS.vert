#version 450

#extension GL_ARB_shader_draw_parameters : require
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : require

#include "../include/common.glsl"

void main()
{
	uint packedID          = getDrawInstanceIDsBuffer().ids[gl_InstanceIndex];
	uint instanceID        = visInstanceID(packedID);
	InstanceInput instance = getInstanceInputBuffer().instanceInputs[instanceID];
	mat4 transform         = getInstanceTransform(instance);
	Vertex vtx             = getVertexBuffer().vertices[gl_VertexIndex];

	SceneData scene = getSceneData();

	vec4 worldPos4 = transform * vec4(vtx.position, 1.0);
	gl_Position    = scene.viewProj * worldPos4;
}
