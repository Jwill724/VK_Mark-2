#version 450

#extension GL_EXT_buffer_reference : require
#extension GL_GOOGLE_include_directive : require

#include "input_structures.glsl"

layout(buffer_reference, std430) readonly buffer VertexBuffer
{
    vec3 vertices[];
};

layout(push_constant) uniform PushConstants
{
    mat4 viewproj;
    VertexBuffer vertexBuffer;
} PC;

void main()
{
    vec3 pos = PC.vertexBuffer.vertices[gl_VertexIndex];
    gl_Position = PC.viewproj * vec4(pos, 1.f);
}