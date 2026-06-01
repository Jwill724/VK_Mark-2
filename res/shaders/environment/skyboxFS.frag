#version 450

#extension GL_ARB_separate_shader_objects : require
#extension GL_GOOGLE_include_directive : require

#include "../include/common.glsl"

layout(location = 0) in vec3 fragPos;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform SkyboxPush
{
	mat4 viewproj;
	uint skyboxID;
} pc;

void main() {
	vec3 dir = normalize(fragPos);
	dir.y = -dir.y;

	SceneData scene = getSceneData();

	vec3 skyColor = SampleCube(pc.skyboxID, dir).rgb;

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
