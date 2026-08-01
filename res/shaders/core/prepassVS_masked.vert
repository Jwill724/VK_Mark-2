#version 450

#extension GL_ARB_shader_draw_parameters : require
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : require

#include "../include/common.glsl"

layout(location = 0) out vec3 outViewNormal;
layout(location = 1) out vec2 outUV;
layout(location = 2) flat out uint outMaterialID;
layout(location = 3) flat out uint outPackedID;

void main()
{
	uint packedID       = getDrawInstanceIDsBuffer().ids[gl_InstanceIndex];
	uint instanceID     = visInstanceID(packedID);
	InstanceInput inst  = getInstanceInputBuffer().instanceInputs[instanceID];
	outMaterialID       = inst.materialID;
	outPackedID         = packedID;

	Vertex vtx = getVertexBuffer().vertices[gl_VertexIndex];
	vec3 position = vtx.position;
	vec3 normal = octDecode(vec2(snorm16ToFloat(int(vtx.normalX)),
							snorm16ToFloat(int(vtx.normalY))));

	vec2 uv = unpackUV(vtx.uvX, vtx.uvY);
	outUV = uv;

	mat4 model = getInstanceTransform(inst);

	SceneData scene = getSceneData();

	mat3 normalMatrix = mat3(scene.view * model);
	outViewNormal     = normalMatrix * normal;

	vec4 worldPos = model * vec4(position, 1.0);
	gl_Position = scene.viewProj * worldPos;
}
