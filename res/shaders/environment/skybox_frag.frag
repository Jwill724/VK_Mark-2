#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

#include "../include/gpu_scene_structures.glsl"

layout(location = 0) in vec3 fragPos;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform EnvMapData {
    EnvMapBindingSet envMapSet;
};

layout(set = 0, binding = 2) uniform samplerCube envMaps[];

void main() {
    vec3 dir = normalize(fragPos);
    dir.y = -dir.y;

    uint skyboxIdx = uint(envMapSet.mapIndices[0].w);

    // fetch the HDR/RGB color
    vec3 skyColor = texture(envMaps[nonuniformEXT(skyboxIdx)], dir).rgb;

    skyColor *= 0.25; // darken image a bit

    outColor = vec4(skyColor, 1.0);
}