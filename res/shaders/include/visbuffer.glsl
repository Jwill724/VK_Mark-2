#ifndef VISBUFFER_GLSL
#define VISBUFFER_GLSL

#define VIS_INVALID 0xFFFFFFFFu

struct VisTriangle
{
	// Per-vertex
	vec3  posWS[3];
	vec4  posCS[3];
	vec2  uv[3];
	vec3  normalOS[3];
	vec3  tangentOS[3];
	vec4  color[3];

	// Per-triangle / flat.
	float tangentHandedness;
	uint  materialID;
	uint  transformID;
	uint  flags;
	mat4  model;
};

// Replicates the hardware index fetch
void fetchTriangleIndices(Mesh mesh, uint primitiveID, out int i0, out int i1, out int i2)
{
	IndexBuffer ib = getIndexBuffer();
	uint base = mesh.firstIndex + primitiveID * 3u;

	i0 = int(ib.indices[base + 0u] + mesh.vertexOffset);
	i1 = int(ib.indices[base + 1u] + mesh.vertexOffset);
	i2 = int(ib.indices[base + 2u] + mesh.vertexOffset);
}

bool isShadowLODIndex(uint lodIdx) { return lodIdx >= LOD_IDX_SHADOW0; }

VisTriangle fetchVisTriangle(uint packedID, uint primitiveID)
{
	VisTriangle tri;

	uint instanceID = visInstanceID(packedID);
	uint lodIdx     = visLODIndex(packedID);

	InstanceInput inst = getInstanceInputBuffer().instanceInputs[instanceID];
	SceneData     scn  = getSceneData();

	tri.materialID  = inst.materialID;
	tri.transformID = inst.transformID;
	tri.flags       = inst.flags;
	tri.model       = getTransformBuffer().transforms[inst.transformID];

	Mesh mesh = getMeshBuffer().meshes[meshFromLODIndex(inst, lodIdx)];

	uint firstIndex = isShadowLODIndex(lodIdx) ? mesh.shadowFirstIndex : mesh.firstIndex;
	uint base       = firstIndex + primitiveID * 3u;

	IndexBuffer ib = getIndexBuffer();
	int idx[3];
	idx[0] = int(ib.indices[base + 0u] + mesh.vertexOffset);
	idx[1] = int(ib.indices[base + 1u] + mesh.vertexOffset);
	idx[2] = int(ib.indices[base + 2u] + mesh.vertexOffset);

	float handedness[3];
	for (int k = 0; k < 3; ++k)
	{
		vec3 posOS;
		unpackVertex(idx[k], tri.uv[k], tri.color[k], tri.normalOS[k],
		             tri.tangentOS[k], handedness[k], posOS);

		vec4 wp      = tri.model * vec4(posOS, 1.0);
		tri.posWS[k] = wp.xyz;
		tri.posCS[k] = scn.viewProj * wp;
	}

	tri.tangentHandedness = handedness[0];   // flat, never interpolated
	return tri;
}

// ================================================
// Interpolated surface, ready to hand to shading.

struct VisSurface
{
	vec3  posWS;
	vec3  normalWS;
	vec3  tangentWS;
	float tangentW;
	vec4  color;
	vec2  uv;
	vec2  uvDdx;
	vec2  uvDdy;
	uint  materialID;
};

VisSurface interpolateVisSurface(VisTriangle tri, BarycentricDeriv bary)
{
	VisSurface s;

	s.posWS = InterpolateVec3(bary, tri.posWS[0], tri.posWS[1], tri.posWS[2]);

	UVGrad uvg = InterpolateUVGrad(bary, tri.uv[0], tri.uv[1], tri.uv[2]);
	s.uv    = uvg.uv;
	s.uvDdx = uvg.ddx;
	s.uvDdy = uvg.ddy;

	vec3 nOS = InterpolateVec3(bary, tri.normalOS[0], tri.normalOS[1], tri.normalOS[2]);
	vec3 tOS = InterpolateVec3(bary, tri.tangentOS[0], tri.tangentOS[1], tri.tangentOS[2]);

	// Non-uniform scale would require the inverse-transpose here.
	mat3 nrmMat = mat3(tri.model);
	s.normalWS  = normalize(nrmMat * nOS);
	s.tangentWS = normalize(nrmMat * tOS);
	s.tangentW  = tri.tangentHandedness;

	s.color      = InterpolateVec4(bary, tri.color[0], tri.color[1], tri.color[2]);
	s.materialID = tri.materialID;

	return s;
}

vec4 SampleVisSurface(uint texID, VisSurface s, float bias)
{
	float scale = exp2(bias);
	return SampleTextureGrad(texID, s.uv, s.uvDdx * scale, s.uvDdy * scale);
}

vec3 EvalShadingNormal(
	VisTriangle tri, Material mat, bool hasNormalMap,
	vec3 lambda, vec2 uvGradX, vec2 uvGradY)
{
	vec3 nOS = lambda.x * tri.normalOS[0]  + lambda.y * tri.normalOS[1]  + lambda.z * tri.normalOS[2];
	vec3 tOS = lambda.x * tri.tangentOS[0] + lambda.y * tri.tangentOS[1] + lambda.z * tri.tangentOS[2];
	vec2 uv  = lambda.x * tri.uv[0]        + lambda.y * tri.uv[1]        + lambda.z * tri.uv[2];

	mat3 nrmMat = mat3(tri.model);
	vec3 Ng = normalize(nrmMat * nOS);

	if (!hasNormalMap) return Ng;

	vec3 Tw = nrmMat * tOS;
	vec3 T  = normalize(Tw - Ng * dot(Ng, Tw));
	vec3 B  = cross(Ng, T) * tri.tangentHandedness;
	mat3 tbn = mat3(T, B, Ng);

	vec3 nt = SampleTextureGrad(mat.normalID, uv, uvGradX, uvGradY).rgb * 2.0 - 1.0;
	nt.xy *= mat.normalScale;
	nt = normalize(nt);

	return normalize(tbn * nt);
}

#endif
