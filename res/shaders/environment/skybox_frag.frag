#version 450

#extension GL_ARB_separate_shader_objects : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

#include "../include/set_bindings.glsl"
#include "../include/gpu_scene_structures.glsl"

layout(location = 0) in vec3 fragPos;
layout(location = 0) out vec4 outColor;

layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_ENV_INDEX) uniform EnvMapData {
	EnvMapIndexArray envMapSet;
};

layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_SAMPLER_CUBE) uniform samplerCube envMaps[];

layout(set = FRAME_SET, binding = FRAME_BINDING_SCENE) uniform SceneUBO {
	SceneData scene;
};

layout(set = GLOBAL_SET, binding = GLOBAL_BINDING_DEBUG_INLINE, scalar) uniform DebugData {
	DebugToggles debug;
};

void main() {
	vec3 dir = normalize(fragPos);
	dir.y = -dir.y;

	uint skyboxIdx = envMapSet.indices[debug.activeEnvMap].w;

	// fetch the HDR color
	vec3 skyColor = texture(envMaps[nonuniformEXT(skyboxIdx)], dir).rgb;

	outColor = vec4(skyColor, 1.0);
}