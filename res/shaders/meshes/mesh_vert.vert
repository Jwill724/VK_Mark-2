#version 450

#extension GL_ARB_shader_draw_parameters : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_ARB_gpu_shader_int64 : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : require

#include "../include/gpu_scene_structures.glsl"

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec2 outUV;
layout(location = 2) out vec3 outWorldPos;
layout(location = 3) flat out uint vDrawID;

layout(set = 0, binding = 0, scalar) readonly buffer globalAddressTableBuffer {
    GPUAddressTable globalAddressTable;
};

layout(set = 1, binding = 0, scalar) readonly buffer frameAddressTableBuffer {
    GPUAddressTable frameAddressTable;
};

layout(set = 1, binding = 1, scalar) uniform SceneUBO {
    SceneData scene;
};

layout(push_constant) uniform PushConstants {
	uint opaqueVisibleCount;
	uint transparentVisibleCount;
	uint pad[2];
} pc;

void main() {
	uint drawID = gl_DrawIDARB;
	vDrawID = drawID;

	IndirectDrawCmd drawCmd;
	Instance inst;
	Mesh mesh;

	if (drawID < pc.opaqueVisibleCount) {
		// === Opaque path ===
		OpaqueIndirectDraws cmdBuf = OpaqueIndirectDraws(frameAddressTable.addrs[ABT_OpaqueIndirectDraws]);
		OpaqueInstances instBuf = OpaqueInstances(frameAddressTable.addrs[ABT_OpaqueInstances]);

		drawCmd = cmdBuf.opaqueIndirect[drawID];
		inst = instBuf.opaqueInstances[drawCmd.instanceIndex];
		mesh = MeshBuffer(globalAddressTable.addrs[ABT_Mesh]).meshes[inst.meshID];
	} else {
		// === Transparent path ===
		uint tIndex = drawID - pc.opaqueVisibleCount;
		if (tIndex >= pc.transparentVisibleCount) return;

		TransparentIndirectDraws cmdBuf = TransparentIndirectDraws(frameAddressTable.addrs[ABT_TrasparentIndirectDraws]);
		TransparentInstances instBuf = TransparentInstances(frameAddressTable.addrs[ABT_TransparentInstances]);

		drawCmd = cmdBuf.transparentIndirect[tIndex];
		inst = instBuf.transparentInstances[drawCmd.instanceIndex];
		mesh = MeshBuffer(globalAddressTable.addrs[ABT_Mesh]).meshes[inst.meshID];
	}

    DrawRangeBuffer drawRangeBuf = DrawRangeBuffer(globalAddressTable.addrs[ABT_DrawRange]);
    GPUDrawRange range = drawRangeBuf.ranges[mesh.drawRangeIndex];

    VertexBuffer vertexBuf = VertexBuffer(globalAddressTable.addrs[ABT_Vertex]);
    IndexBuffer indexBuf = IndexBuffer(globalAddressTable.addrs[ABT_Index]);

    uint vertexIndex = indexBuf.indices[range.firstIndex + gl_VertexIndex];
    Vertex vtx = vertexBuf.vertices[vertexIndex];

    mat4 model = inst.modelMatrix;
    vec4 worldPos4 = model * vec4(vtx.position, 1.0);
    gl_Position = scene.viewproj * worldPos4;
    outWorldPos = worldPos4.xyz;

    mat3 normalMatrix = transpose(inverse(mat3(model)));
    outNormal = normalize(normalMatrix * vtx.normal);

    outUV = vtx.uv;
}