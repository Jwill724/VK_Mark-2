#version 450

layout(push_constant) uniform PushConstants
{
    mat4 viewproj;
} pc;

layout(location = 0) out vec3 texCoord;

vec3 positions[36] = vec3[](
    // +X face
    vec3(1, -1, -1), vec3(1, 1, -1), vec3(1, 1, 1),
    vec3(1, -1, -1), vec3(1, 1, 1), vec3(1, -1, 1),
    // -X face
    vec3(-1, -1, -1), vec3(-1, 1, 1), vec3(-1, 1, -1),
    vec3(-1, -1, -1), vec3(-1, -1, 1), vec3(-1, 1, 1),
    // +Y face
    vec3(-1, 1, -1), vec3(1, 1, -1), vec3(1, 1, 1),
    vec3(-1, 1, -1), vec3(1, 1, 1), vec3(-1, 1, 1),
    // -Y face
    vec3(-1, -1, -1), vec3(1, -1, 1), vec3(1, -1, -1),
    vec3(-1, -1, -1), vec3(-1, -1, 1), vec3(1, -1, 1),
    // +Z face
    vec3(-1, -1, 1), vec3(1, -1, 1), vec3(1, 1, 1),
    vec3(-1, -1, 1), vec3(1, 1, 1), vec3(-1, 1, 1),
    // -Z face
    vec3(-1, -1, -1), vec3(1, 1, -1), vec3(1, -1, -1),
    vec3(-1, -1, -1), vec3(-1, 1, -1), vec3(1, 1, -1)
);

void main()
{
    texCoord = positions[gl_VertexIndex];

    vec4 pos = pc.viewproj * vec4(texCoord, 1.0);
    pos.z = pos.w; // Force it to far plane
    gl_Position = pos;
}