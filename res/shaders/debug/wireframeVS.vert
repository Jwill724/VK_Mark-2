#version 450

#extension GL_ARB_shader_draw_parameters : require
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : require

#include "../include/common.glsl"

layout(location = 0) out vec4 outColor;

void main()
{
	Instance inst = getInstanceBuffer().instances[gl_InstanceIndex];
	Vertex vtx = getVertexBuffer().vertices[gl_VertexIndex];
	outColor = unpackRGBA8(vtx.colorRGBA8);

	mat4 model = getTransformBuffer().transforms[inst.transformID];
	SceneData scene = getSceneData();

	vec4 worldPos4 = model * vec4(vtx.position, 1.0);
	gl_Position    = scene.viewProj * worldPos4;
}
