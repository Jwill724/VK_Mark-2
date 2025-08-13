#version 450

#extension GL_ARB_shader_draw_parameters : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : require
#extension GL_EXT_nonuniform_qualifier : require

#include "../include/set_bindings.glsl"
#include "../include/gpu_scene_structures.glsl"

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec3 outColor;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec3 outWorldPos;
layout(location = 4) flat out uint outMaterialID;

layout(set = GLOBAL_SET, binding = ADDRESS_TABLE_BINDING, scalar) readonly buffer GlobalAddressTableBuffer {
	GPUAddressTable globalAddressTable;
};

layout(set = FRAME_SET, binding = ADDRESS_TABLE_BINDING, scalar) readonly buffer FrameAddressTableBuffer {
	GPUAddressTable frameAddressTable;
};

layout(set = FRAME_SET, binding = FRAME_BINDING_SCENE) uniform SceneUBO {
	SceneData scene;
};

layout(push_constant) uniform DrawPushConstants {
	uint drawPassType;
	uint totalVertexCount;
	uint totalIndexCount;
	uint totalMeshCount;
	uint totalMaterialCount;
	uint pad0[3];
} drawDataPC;

void main()
{
	Instance inst;
	if (drawDataPC.drawPassType == 0u) {
		OpaqueInstances ib = OpaqueInstances(frameAddressTable.addrs[ABT_OpaqueInstances]);
		inst = ib.opaqueInstances[gl_InstanceIndex];
	} else {
		TransparentInstances ib = TransparentInstances(frameAddressTable.addrs[ABT_TransparentInstances]);
		inst = ib.transparentInstances[gl_InstanceIndex];
	}

	outMaterialID = inst.materialID;

	if (gl_VertexIndex >= drawDataPC.totalVertexCount) {
		gl_Position = vec4(2e9, 2e9, 2e9, 1.0); // push off-screen
		return;
	}

	// fetch vertex
	Vertex vtx = VertexBuffer(globalAddressTable.addrs[ABT_Vertex]).vertices[gl_VertexIndex];

	// fetch transform
	mat4 model = TransformsListBuffer(frameAddressTable.addrs[ABT_Transforms]).transforms[inst.transformID];

	vec4 worldPos4 = model * vec4(vtx.position, 1.0);
	outWorldPos  = worldPos4.xyz;
	gl_Position = scene.viewproj * worldPos4;

	mat3 normalMatrix = transpose(inverse(mat3(model)));
	outNormal = normalize(normalMatrix * vtx.normal);
	outColor = vtx.color.xyz;
	outUV = vtx.uv;
}