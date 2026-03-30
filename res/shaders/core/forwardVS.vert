#version 450

#extension GL_ARB_shader_draw_parameters : require
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : require

#include "../include/common.glsl"

layout(location = 0) out vec3 outColor;
layout(location = 1) out vec2 outUV;
layout(location = 2) out vec3 outWorldPos;
layout(location = 3) out vec4 outViewPos;
layout(location = 4) out vec3 outNormal;
layout(location = 5) out vec3 outTangent;
layout(location = 6) out float outTangentW;
layout(location = 7) flat out uint outMaterialID;

void main() {
	Instance inst = getInstanceBuffer().instances[gl_InstanceIndex];
	outMaterialID = inst.materialID;

	vec2 uv;
	vec4 color;
	vec3 normal;
	vec3 tangent;
	float tangentW;
	vec3 position;
	unpackVertex(
		gl_VertexIndex,
		uv,
		color,
		normal,
		tangent,
		tangentW,
		position);

	mat4 model = getTransformBuffer().transforms[inst.transformID];
	SceneData scene = getSceneData();

	vec4 worldPos4 = model * vec4(position, 1.0);
	outWorldPos    = worldPos4.xyz;
	outViewPos     = scene.view * vec4(worldPos4.xyz, 1.0);
	gl_Position    = scene.viewProj * worldPos4;

	mat3 normalMat = mat3(model);
	outColor    = color.xyz;
	outUV       = uv;
	outNormal   = normalMat * normal;
	outTangent  = normalMat * tangent;
	outTangentW = tangentW;
}
