#version 450

#extension GL_ARB_shader_draw_parameters : require
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : require

#include "../include/set_bindings.glsl"
#include "../include/gpu_scene_structures.glsl"

layout(location = 0) out vec3 outColor;
layout(location = 1) out vec2 outUV;
layout(location = 2) out vec3 outWorldPos;
layout(location = 3) out vec4 outViewPos;
layout(location = 4) out vec3 outNormal;
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

	// pass over to the frag shader
	outMaterialID = inst.materialID;

	// fetch vertex
	Vertex vtx = VertexBuffer(globalAddressTable.addrs[ABT_Vertex]).vertices[gl_VertexIndex];
	vec2 uv = unpackUV(vtx.uvX, vtx.uvY);
	vec4 color = unpackRGBA8(vtx.colorRGBA8);
    vec2 octEnc;
	octEnc.x = snorm16ToFloat(vtx.normalX);
	octEnc.y = snorm16ToFloat(vtx.normalY);
	vec3 normal = octDecode(octEnc);

	// fetch transform
	mat4 model = TransformsBuffer(frameAddressTable.addrs[ABT_Transforms]).transforms[inst.transformID];

	vec4 worldPos4 = model * vec4(vtx.position, 1.0);
	outWorldPos = worldPos4.xyz;
	outViewPos = scene.view * vec4(worldPos4.xyz, 1.0);
	gl_Position = scene.viewProj * worldPos4;

	outColor = color.xyz;
	outUV = uv;
	outNormal = mat3(model) * normal;
}
