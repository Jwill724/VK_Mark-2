#version 450

#extension GL_EXT_buffer_reference : require

layout(buffer_reference, std430) readonly buffer VertexBuffer
{
    vec3 vertices[];
};

layout(push_constant) uniform PushConstants
{
    mat4 viewproj;
    VertexBuffer vertexBuffer;
	uint pad0[2];
} PC;

void main()
{
    vec3 pos = PC.vertexBuffer.vertices[gl_VertexIndex];

    gl_Position = PC.viewproj * vec4(pos, 1.f);
}