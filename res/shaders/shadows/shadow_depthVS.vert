#version 450

#extension GL_ARB_shader_draw_parameters : require
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : require

#include "../include/set_bindings.glsl"
#include "../include/gpu_scene_structures.glsl"

layout(set = GLOBAL_SET, binding = ADDRESS_TABLE_BINDING, scalar) readonly buffer GlobalAddressTableBuffer {
	GPUAddressTable globalAddressTable;
};

layout(set = FRAME_SET, binding = ADDRESS_TABLE_BINDING, scalar) readonly buffer FrameAddressTableBuffer {
	GPUAddressTable frameAddressTable;
};

layout(push_constant) uniform ShadowPush {
	mat4 viewproj;
} pc;

void main()
{
	// fetch shadow caster instances
	Instance inst = VisibleInstances(frameAddressTable.addrs[ABT_VisibleInstances]).instances[gl_InstanceIndex];

	// fetch vertex
	Vertex vtx = VertexBuffer(globalAddressTable.addrs[ABT_Vertex]).vertices[gl_VertexIndex];

	// fetch transform
	mat4 model = TransformsBuffer(frameAddressTable.addrs[ABT_Transforms]).transforms[inst.transformID];

	vec4 worldPos = model * vec4(vtx.position, 1.0);

	gl_Position = pc.viewproj * worldPos;
}
