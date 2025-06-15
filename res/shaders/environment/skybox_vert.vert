#version 450

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_ARB_gpu_shader_int64 : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : enable

layout(push_constant) uniform PC
{
    mat4 viewproj;
} pc;

layout(location = 0) out vec3 texCoord;

vec3 positions[3] = vec3[](
        vec3(-1.0, -1.0, 0.001),
        vec3( 3.0, -1.0, 0.001),
        vec3(-1.0,  3.0, 0.001)
);

void main()
{
    vec3 pos = positions[gl_VertexIndex];
	gl_Position = vec4(pos, 1.0);
	texCoord = (pc.viewproj * vec4(pos, 1.0)).xyz;
}