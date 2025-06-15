#version 450

#extension GL_ARB_shader_draw_parameters : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_ARB_gpu_shader_int64 : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : require

#include "../include/input_structures.glsl"

#define gl_DrawID gl_DrawIDARB

layout(location = 0) out vec3 outNormal;
layout(location = 1) out vec3 outColor;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec3 outWorldPos;
layout(location = 4) flat out uint vDrawID;

layout(set = 0, binding = 0, std430) readonly buffer globalAddressTableBuffer {
    GPUAddressTable globalAddressTable;
};

layout(set = 1, binding = 0, std430) readonly buffer frameAddressTableBuffer {
    GPUAddressTable frameAddressTable;
};

layout(set = 1, binding = 1) uniform SceneUBO {
    SceneData scene;
};

layout(push_constant) uniform PushConstants {
    DrawPushConstants pc;
};

void main()
{
    // === Buffers ===
    InstanceBuffer instanceBuf = InstanceBuffer(frameAddressTable.instanceBuffer);
    IndirectBuffer drawCmdBuf = IndirectBuffer(frameAddressTable.indirectCmdBuffer);
    MaterialBuffer materialBuf = MaterialBuffer(globalAddressTable.materialBuffer);
    DrawRangeBuffer rangeBuf  = DrawRangeBuffer(globalAddressTable.drawRangeBuffer);

    VertexBuffer vertexBuf;
    IndexBuffer  indexBuf;

    Instance inst;
    Material mat;

    // === DrawID check ===
    uint drawID = gl_DrawID;
    vDrawID = drawID;
    if (drawID == 0) {
        inst.modelMatrix = pc.modelMatrix;
        inst.vertexAddress = pc.vertexAddress;
        inst.indexAddress = pc.indexAddress;
        inst.drawRangeIndex = pc.drawRangeIndex;
        mat = materialBuf.materials[pc.materialIndex];
    } else {
        IndirectDrawCmd drawCmd = drawCmdBuf.cmds[drawID];
        uint instanceIndex = drawCmd.instanceIndex;
        inst = instanceBuf.instances[instanceIndex];
        mat = materialBuf.materials[inst.materialIndex];
    }

    vertexBuf = VertexBuffer(inst.vertexAddress);
    indexBuf  = IndexBuffer(inst.indexAddress);

    GPUDrawRange range = rangeBuf.ranges[inst.drawRangeIndex];
    uint vertexIndex = indexBuf.indices[range.firstIndex + gl_VertexIndex];
    Vertex vtx = vertexBuf.vertices[vertexIndex];

    mat4 model = inst.modelMatrix;
    vec4 worldPos4 = model * vec4(vtx.position, 1.0);
    gl_Position = scene.viewproj * worldPos4;
    outWorldPos = worldPos4.xyz;

    mat3 normalMatrix = transpose(inverse(mat3(model)));
    outNormal = normalize(normalMatrix * vtx.normal);

    outColor = vtx.color.rgb * mat.colorFactor.rgb;
    outUV = vtx.uv;
}