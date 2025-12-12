#version 450

#extension GL_ARB_shader_draw_parameters : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : require
#extension GL_EXT_multiview : require

#include "../include/set_bindings.glsl"
#include "../include/gpu_scene_structures.glsl"
#include "../include/shadow.glsl"

layout(set = GLOBAL_SET, binding = ADDRESS_TABLE_BINDING, scalar) readonly buffer GlobalAddressTableBuffer {
	GPUAddressTable globalAddressTable;
};

layout(set = FRAME_SET, binding = ADDRESS_TABLE_BINDING, scalar) readonly buffer FrameAddressTableBuffer {
	GPUAddressTable frameAddressTable;
};

layout(set = FRAME_SET, binding = FRAME_BINDING_CSM) uniform ShadowUBO {
	ShadowCSM csm;
};

void main()
{
	// fetch instance
	Instance inst = VisibleInstances(frameAddressTable.addrs[ABT_VisibleInstances]).instances[gl_InstanceIndex];

	// fetch vertex
	Vertex vtx = VertexBuffer(globalAddressTable.addrs[ABT_Vertex]).vertices[gl_VertexIndex];

	// fetch transform
	mat4 model = TransformsBuffer(globalAddressTable.addrs[ABT_Transforms]).transforms[inst.transformID];

	vec4 worldPos = model * vec4(vtx.position, 1.0);

	// use the cascade's light-space matrix
	gl_Position = csm.cascadeVP[gl_ViewIndex] * worldPos;
}