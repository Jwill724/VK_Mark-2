#version 450

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_ARB_separate_shader_objects : require

layout(buffer_reference, scalar) readonly buffer VertexBuffer
{
    vec3 vertices[];
};

layout(push_constant) uniform PushConstants
{
    mat4 viewproj;
    VertexBuffer vertexBuffer;
    uint pad0[2];
} pc;

void main()
{
    vec3 pos = pc.vertexBuffer.vertices[gl_VertexIndex];

    gl_Position = pc.viewproj * vec4(pos, 1.f);
}