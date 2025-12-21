#version 450

#extension GL_ARB_shader_draw_parameters : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : require

#include "../include/set_bindings.glsl"
#include "../include/gpu_scene_structures.glsl"

layout(location = 0) out vec3 outViewNormal;
layout(location = 1) out vec4 outCurrClipPos;
layout(location = 2) out vec4 outPrevClipPos;
layout(location = 4) flat out uint outTemporalValidation;

layout(set = GLOBAL_SET, binding = ADDRESS_TABLE_BINDING, scalar) readonly buffer GlobalAddressTableBuffer {
	GPUAddressTable globalAddressTable;
};

layout(set = FRAME_SET, binding = ADDRESS_TABLE_BINDING, scalar) readonly buffer FrameAddressTableBuffer {
	GPUAddressTable frameAddressTable;
};

layout(set = FRAME_SET, binding = FRAME_BINDING_SCENE) uniform SceneUBO {
	SceneData scene;
};

void main()
{
	// fetch instance
	Instance inst = VisibleInstances(frameAddressTable.addrs[ABT_VisibleInstances]).instances[gl_InstanceIndex];

	// fetch vertex
	Vertex vtx = VertexBuffer(globalAddressTable.addrs[ABT_Vertex]).vertices[gl_VertexIndex];

	// fetch transform
	mat4 model = TransformsBuffer(globalAddressTable.addrs[ABT_Transforms]).transforms[inst.transformID];

	mat4 prevModel = model;
	if (scene.temporal.y == 1) {
		prevModel = PrevTransformsBuffer(globalAddressTable.addrs[ABT_PrevTransforms]).prevTransforms[inst.transformID];
	}
	outTemporalValidation = scene.temporal.y;

	// World space -> view space
	mat3 normalMatrix = mat3(scene.view * model);
	outViewNormal = normalMatrix * vtx.normal;

	vec4 worldPos = model * vec4(vtx.position, 1.0);
	vec4 prevWorldPos = prevModel * vec4(vtx.position, 1.0);

	outCurrClipPos = scene.viewProj * worldPos;
	outPrevClipPos = scene.prevViewProj * prevWorldPos;

	gl_Position = outCurrClipPos;
}