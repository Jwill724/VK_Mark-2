#version 450

#extension GL_ARB_shader_draw_parameters : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : require

#include "../include/set_bindings.glsl"
#include "../include/gpu_scene_structures.glsl"

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec3 outColor;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec3 outWorldPos;
layout(location = 4) out vec4 outViewPos;
layout(location = 5) flat out uint outMaterialID;

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

	// pass over to the frag shader
	outMaterialID = inst.materialID;

	// fetch vertex
	Vertex vtx = VertexBuffer(globalAddressTable.addrs[ABT_Vertex]).vertices[gl_VertexIndex];

	// fetch transform
	mat4 model = TransformsBuffer(globalAddressTable.addrs[ABT_Transforms]).transforms[inst.transformID];

	vec4 worldPos4 = model * vec4(vtx.position, 1.0);
	outWorldPos = worldPos4.xyz;
	outViewPos = scene.view * vec4(worldPos4.xyz, 1.0);
	gl_Position = scene.viewproj * worldPos4;

	mat3 normalMatrix = transpose(inverse(mat3(model)));
	outNormal = normalize(normalMatrix * vtx.normal);
	outColor = vtx.color.xyz;
	outUV = vtx.uv;
}