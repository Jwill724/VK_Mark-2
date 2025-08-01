#version 450

#extension GL_ARB_shader_draw_parameters : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_ARB_gpu_shader_int64 : require
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : require
#extension GL_EXT_nonuniform_qualifier : require

#include "../include/gpu_scene_structures.glsl"

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec3 outColor;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec3 outWorldPos;
layout(location = 4) flat out uint vDrawID;
layout(location = 5) flat out uint vInstanceID;

layout(set = 0, binding = 0, scalar) readonly buffer GlobalAddressTableBuffer {
    GPUAddressTable globalAddressTable;
};

layout(set = 1, binding = 0, scalar) readonly buffer FrameAddressTableBuffer {
    GPUAddressTable frameAddressTable;
};

layout(set = 1, binding = 1) uniform SceneUBO {
    SceneData scene;
};

layout(push_constant) uniform DrawPushConstants {
	uint opaqueDrawCount;
	uint transparentDrawCount;
	uint totalVertexCount;
	uint totalIndexCount;
	uint totalMeshCount;
	uint totalMaterialCount;
	uint pad0[2];
} drawData;

void main() {
    vDrawID = gl_DrawIDARB;

    IndirectDrawCmd drawCmd;
    Instance inst;

    if (vDrawID < drawData.opaqueDrawCount) {
        // opaque
        OpaqueIndirectDraws cmdBuf = OpaqueIndirectDraws(frameAddressTable.addrs[ABT_OpaqueIndirectDraws]);
        OpaqueInstances instBuf = OpaqueInstances(frameAddressTable.addrs[ABT_OpaqueInstances]);

        drawCmd     = cmdBuf.opaqueIndirect[vDrawID];
        vInstanceID = drawCmd.firstInstance + gl_InstanceIndex;
        inst        = instBuf.opaqueInstances[vInstanceID];
    } else {
        // transparent
        uint tIndex = vDrawID - drawData.opaqueDrawCount;
        if (tIndex >= drawData.transparentDrawCount) return;

        TransparentIndirectDraws cmdBuf = TransparentIndirectDraws(frameAddressTable.addrs[ABT_TransparentIndirectDraws]);
        TransparentInstances instBuf = TransparentInstances(frameAddressTable.addrs[ABT_TransparentInstances]);

        drawCmd     = cmdBuf.transparentIndirect[tIndex];
        vInstanceID = drawCmd.firstInstance + gl_InstanceIndex;
        inst        = instBuf.transparentInstances[vInstanceID];
    }

    uint vertIdx = gl_VertexIndex;
    if (vertIdx >= drawData.totalVertexCount) return;

    // fetch vertex
    Vertex vtx = VertexBuffer(globalAddressTable.addrs[ABT_Vertex]).vertices[vertIdx];

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