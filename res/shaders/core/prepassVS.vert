#version 450

#extension GL_ARB_shader_draw_parameters : require
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : require

#include "../include/set_bindings.glsl"
#include "../include/gpu_scene_structures.glsl"

layout(location = 0) out vec3 outViewNormal;
layout(location = 1) out vec2 outCurrNdc;
layout(location = 2) out vec2 outPrevNdc;
layout(location = 3) out vec2 outUV;
layout(location = 4) flat out uint outTemporalValidation;
layout(location = 5) flat out uint outMaterialID;

layout(set = GLOBAL_SET, binding = ADDRESS_TABLE_BINDING, scalar) readonly buffer GlobalAddressTableBuffer {
	GPUAddressTable globalAddressTable;
};

layout(set = FRAME_SET, binding = ADDRESS_TABLE_BINDING, scalar) readonly buffer FrameAddressTableBuffer {
	GPUAddressTable frameAddressTable;
};

layout(set = FRAME_SET, binding = FRAME_BINDING_SCENE) uniform SceneUBO {
	SceneData scene;
};

void main()
{
	// fetch instance
	Instance inst = VisibleInstances(frameAddressTable.addrs[ABT_VisibleInstances]).instances[gl_InstanceIndex];

	// fetch vertex
	Vertex vtx = VertexBuffer(globalAddressTable.addrs[ABT_Vertex]).vertices[gl_VertexIndex];
	vec2 octEnc;
	octEnc.x = snorm16ToFloat(vtx.normalX);
	octEnc.y = snorm16ToFloat(vtx.normalY);
	vec3 normal = octDecode(octEnc);
	vec2 uv = unpackUV(vtx.uvX, vtx.uvY);

	// fetch transform
	mat4 model = TransformsBuffer(frameAddressTable.addrs[ABT_Transforms]).transforms[inst.transformID];

	mat4 prevModel = model;
	if (scene.temporal.y == 1u) {
		prevModel = PrevTransformsBuffer(frameAddressTable.addrs[ABT_PrevTransforms]).prevTransforms[inst.transformID];
	}

	// World space -> view space
	mat3 normalMatrix = mat3(scene.view * model);
	outViewNormal = normalMatrix * normal;

	vec4 worldPos = model * vec4(vtx.position, 1.0);
	vec4 prevWorldPos = prevModel * vec4(vtx.position, 1.0);

	vec4 currClipPos = scene.viewProj * worldPos;
	vec4 prevClipPos = scene.prevViewProj * prevWorldPos;
	outUV = uv;
	outMaterialID = inst.materialID;

	bool currValid = currClipPos.w > 0.0;
	bool prevValid = prevClipPos.w > 0.0;

	if (!currValid || !prevValid) {
		outCurrNdc = vec2(0.0);
		outPrevNdc = vec2(0.0);
		outTemporalValidation = 0u;
	}
	else {
		outCurrNdc = currClipPos.xy / currClipPos.w;
		outPrevNdc = prevClipPos.xy / prevClipPos.w;
		outTemporalValidation = scene.temporal.y;
	}

	gl_Position = currClipPos;
}
