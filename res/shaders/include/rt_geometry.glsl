#ifndef RT_GEOMETRY_GLSL
#define RT_GEOMETRY_GLSL

#extension GL_GOOGLE_include_directive : require

#include "rt_core.glsl"

struct HitSurface
{
	vec3  position;
	vec3  normal;
	vec3  geoNormal;
	vec3  albedo;
	vec3  emissive;
	float roughness;
	float metallic;
	bool  frontFace;
	Material mat;
};

void rtFetchIndices(Mesh mesh, uint primID, out int i0, out int i1, out int i2)
{
	IndexBuffer ib = getIndexBuffer();
	uint base = mesh.firstIndex + primID * 3u;

	i0 = int(ib.indices[base + 0u] + mesh.vertexOffset);
	i1 = int(ib.indices[base + 1u] + mesh.vertexOffset);
	i2 = int(ib.indices[base + 2u] + mesh.vertexOffset);
}

bool rtAlphaTestPasses(uint instanceID, uint primID, vec2 bary, float mipBias)
{
	InstanceInput inst = getInstanceInputBuffer().instanceInputs[instanceID];
	if ((inst.flags & ALPHA_TESTED) == 0u) return true;

	Mesh mesh = getMeshBuffer().meshes[rtBlasMeshID(inst)];
	Material mat = getMaterialBuffer().materials[inst.materialID];

	int idx[3];
	rtFetchIndices(mesh, primID, idx[0], idx[1], idx[2]);

	VertexBuffer vb = getVertexBuffer();
	vec2 uv0 = unpackUV(vb.vertices[idx[0]].uvX, vb.vertices[idx[0]].uvY);
	vec2 uv1 = unpackUV(vb.vertices[idx[1]].uvX, vb.vertices[idx[1]].uvY);
	vec2 uv2 = unpackUV(vb.vertices[idx[2]].uvX, vb.vertices[idx[2]].uvY);

	float w = 1.0 - bary.x - bary.y;
	vec2 uv = uv0 * w + uv1 * bary.x + uv2 * bary.y;

	float a = SampleTextureLod(mat.albedoID, uv, mipBias).a * mat.colorFactor.a;
	return a >= mat.alphaCutoff;
}

HitSurface rtUnpackHit(
	uint instanceID, uint primID, vec2 bary,
	float t, vec3 origin, vec3 dir, float mipBias)
{
	InstanceInput inst = getInstanceInputBuffer().instanceInputs[instanceID];
	Mesh mesh = getMeshBuffer().meshes[rtBlasMeshID(inst)];
	Material mat = getMaterialBuffer().materials[inst.materialID];

	int idx[3];
	rtFetchIndices(mesh, primID, idx[0], idx[1], idx[2]);

	vec2 uv[3];
	vec4 col[3];
	vec3 nOS[3];
	vec3 tOS[3];
	float hand[3];
	vec3 pos[3];

	for (int k = 0; k < 3; ++k)
		unpackVertex(idx[k], uv[k], col[k], nOS[k], tOS[k], hand[k], pos[k]);

	float w = 1.0 - bary.x - bary.y;
	vec2 hitUV = uv[0] * w + uv[1] * bary.x + uv[2] * bary.y;
	vec3 nLocal = normalize(nOS[0] * w + nOS[1] * bary.x + nOS[2] * bary.y);

	mat4 model = getInstanceTransform(inst);
	vec3 worldNormal = normalize(mat3(model) * nLocal);

	HitSurface s;
	s.position  = origin + dir * t;
	s.geoNormal = worldNormal;
	s.frontFace = dot(worldNormal, dir) < 0.0;
	s.normal    = s.frontFace ? worldNormal : -worldNormal;

	s.albedo = SampleTextureLod(mat.albedoID, hitUV, mipBias).rgb * mat.colorFactor.rgb;

	vec3 mr = SampleTextureLod(mat.metalRoughnessID, hitUV, mipBias).rgb;
	s.metallic  = mr.b * mat.metalRoughFactors.x;
	s.roughness = mr.g * mat.metalRoughFactors.y;

	s.emissive = SampleTextureLod(mat.emissiveID, hitUV, mipBias).rgb
			   * mat.emissiveColor * mat.emissiveStrength;

	s.mat = mat;

	return s;
}

#endif
