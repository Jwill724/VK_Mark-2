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
	vec3 skyColor = texture(envMaps[nonuniformEXT(skyboxIdx)], dir).rgb;

	vec3 sunDir = normalize(scene.sunlightDirection.xyz);
	sunDir.y = -sunDir.y;

	float cosTheta = dot(dir, sunDir);

	const float sunRadiusDeg = 0.9;
	const float sunSoftEdge = 0.25;

	float innerAngle = radians(sunRadiusDeg);
	float outerAngle = radians(sunRadiusDeg * (1.0 + sunSoftEdge));

	float cosInner = cos(innerAngle);
	float cosOuter = cos(outerAngle);

	float sunCore = smoothstep(cosOuter, cosInner, cosTheta);
	float sunGlow = pow(clamp(cosTheta, 0.0, 1.0), 1024.0);

	const float coreIntensity = 80.0;
	const float glowIntensity = 300.0;

	vec3 sunColor = vec3(1.0, 0.95, 0.85);

	skyColor += sunColor * (sunCore * coreIntensity + sunGlow * glowIntensity);

	outColor = vec4(skyColor, 1.0);
}
