#version 450

#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_ARB_gpu_shader_int64 : enable
#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : require

#include "../include/input_structures.glsl"

layout(location = 0) in vec3 fragPos;
layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 1) uniform SceneUBO {
    SceneData scene;
};

layout(set = 0, binding = 1) uniform samplerCube envMaps[];

void main()
{
    vec3 dir = normalize(fragPos);
    dir.y =  -dir.y;

    // fetch the HDR/RGB color
    vec3 skyColor = texture(envMaps[nonuniformEXT(uint(scene.envMapIndex.w))], dir).rgb;

    skyColor *= 0.25; // darken image a bit

    outColor = vec4(skyColor, 1.0);
}