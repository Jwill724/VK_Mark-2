#version 450

#extension GL_ARB_shader_draw_parameters : require
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : require

#include "../include/common.glsl"

layout(location = 0) out vec3 outViewNormal;
layout(location = 1) out vec2 outCurrNdc;
layout(location = 2) out vec2 outPrevNdc;
layout(location = 3) out vec2 outUV;
layout(location = 4) flat out uint outTemporalValidation;
layout(location = 5) flat out uint outMaterialID;

void main()
{
	Instance inst = getInstanceBuffer().instances[gl_InstanceIndex];
	outMaterialID = inst.materialID;

	vec2 uv;
	vec3 normal;
	vec3 position;
	unpackVertexMinimal(
		gl_VertexIndex,
		uv,
		normal,
		position);

	// fetch transform
	mat4 model = getTransformBuffer().transforms[inst.transformID];

	SceneData scene = getSceneData();

	mat4 prevModel = model;
	if (scene.temporal.y == 1u) {
		prevModel = getPrevTransformBuffer().prevTransforms[inst.transformID];
	}

	// World space -> view space
	mat3 normalMatrix = mat3(scene.view * model);
	outViewNormal     = normalMatrix * normal;

	vec4 worldPos     = model * vec4(position, 1.0);
	vec4 prevWorldPos = prevModel * vec4(position, 1.0);

	vec4 currClipPos = scene.viewProj * worldPos;

	vec4 currClipUnj = scene.viewProjUnjittered * worldPos;
	vec4 prevClipUnj = scene.prevViewProj * prevWorldPos;

	outUV = uv;
	bool currValid = currClipPos.w > 0.0 && prevClipUnj.w > 0.0;
	if (!currValid) {
		outCurrNdc = vec2(0.0);
		outPrevNdc = vec2(0.0);
		outTemporalValidation = 0u;
	} else {
		outCurrNdc = currClipUnj.xy / currClipUnj.w; // unjittered
		outPrevNdc = prevClipUnj.xy / prevClipUnj.w; // unjittered
		outTemporalValidation = scene.temporal.y;
	}

	gl_Position = currClipPos;
}
