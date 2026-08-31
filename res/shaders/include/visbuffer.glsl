#ifndef VISBUFFER_GLSL
#define VISBUFFER_GLSL

#define VIS_INVALID 0xFFFFFFFFu

const uint LOD_PACK_SHIFT   = 28u;
const uint LOD_PACK_PAYLOAD = (1u << LOD_PACK_SHIFT) - 1u;   // 0x0FFFFFFF
const uint LOD_PACK_MASK    = 7u;
const uint LOD_PACK_TRIP    = 1u << 31u;

uint visMeshletID(uint g)   { return g >> 8u; }
uint visLocalTri(uint g)    { return (g >> 1u) & 0x7Fu; }
bool visFrontFacing(uint g) { return (g & 1u) != 0u; }

uint packVisID(uint instanceID, uint lodIdx)
{
	return (instanceID & LOD_PACK_PAYLOAD)
		 | ((lodIdx & LOD_PACK_MASK) << LOD_PACK_SHIFT)
		 | LOD_PACK_TRIP;
}
uint visInstanceID(uint packed) { return packed & LOD_PACK_PAYLOAD; }
uint visLODIndex(uint packed)   { return (packed >> LOD_PACK_SHIFT) & LOD_PACK_MASK; }

uint packStreamBin(uint binID, uint lodIdx)
{
	return (binID & LOD_PACK_PAYLOAD) | ((lodIdx & LOD_PACK_MASK) << LOD_PACK_SHIFT);
}
uint streamBinID(uint packed)    { return packed & LOD_PACK_PAYLOAD; }
uint streamLODIndex(uint packed) { return (packed >> LOD_PACK_SHIFT) & LOD_PACK_MASK; }

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

VisTriangle fetchVisTriangleMeshlet(uint packedID, uint visG)
{
	VisTriangle tri;

	uint instanceID = visInstanceID(packedID);
	uint lodIdx     = visLODIndex(packedID);

	InstanceInput inst = getInstanceInputBuffer().instanceInputs[instanceID];
	SceneData     scn  = getSceneData();

	tri.materialID  = inst.materialID;
	tri.transformID = inst.transformID;
	tri.flags       = inst.flags;
	tri.model       = getInstanceTransform(inst);

	Mesh    mesh = getMeshBuffer().meshes[meshFromLODIndex(inst, lodIdx)];
	Meshlet ml   = getMeshletBuffer().meshlets[visMeshletID(visG)];

	uint triBase = ml.triangleOffset + visLocalTri(visG) * 3u;

	MeshletTrisBuffer  mt = getMeshletTrisBuffer();
	MeshletVertsBuffer mv = getMeshletVertsBuffer();

	int idx[3];
	for (int k = 0; k < 3; ++k)
	{
		uint localVtx = uint(mt.tris[triBase + uint(k)]);
		idx[k] = int(mv.verts[ml.vertexOffset + localVtx] + mesh.vertexOffset);
	}

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

	tri.tangentHandedness = handedness[0];
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
	return SampleTextureGradTAA(texID, s.uv, s.uvDdx, s.uvDdy, bias);
}

vec3 EvalShadingNormal(
	VisTriangle tri, Material mat, bool hasNormalMap,
	vec3 lambda, vec2 uvGradX, vec2 uvGradY, float faceSign)
{
	vec3 nOS = lambda.x * tri.normalOS[0]  + lambda.y * tri.normalOS[1]  + lambda.z * tri.normalOS[2];
	vec3 tOS = lambda.x * tri.tangentOS[0] + lambda.y * tri.tangentOS[1] + lambda.z * tri.tangentOS[2];
	vec2 uv  = lambda.x * tri.uv[0]        + lambda.y * tri.uv[1]        + lambda.z * tri.uv[2];

	mat3 nrmMat = mat3(tri.model);

	vec3  Ng     = nrmMat * nOS;
	float nLen2  = dot(Ng, Ng);
	if (!(nLen2 > 1e-16))
	{
		Ng    = cross(tri.posWS[1] - tri.posWS[0], tri.posWS[2] - tri.posWS[0]);
		nLen2 = dot(Ng, Ng);
		if (!(nLen2 > 1e-16)) return vec3(0.0, 0.0, 1.0);
	}
	Ng *= inversesqrt(nLen2);
	Ng *= faceSign;

	if (!hasNormalMap) return Ng;

	vec3  Tw    = nrmMat * tOS;
	Tw         -= Ng * dot(Ng, Tw);
	float tLen2 = dot(Tw, Tw);
	if (!(tLen2 > 1e-12)) return Ng;
	vec3 T = Tw * inversesqrt(tLen2);

	float w = (tri.tangentHandedness < 0.0) ? -1.0 : 1.0;
	vec3  B = cross(Ng, T) * w;

	vec2 ntxy = SampleTextureGrad(mat.normalID, uv, uvGradX, uvGradY).rg * 2.0 - 1.0;
	ntxy *= mat.normalScale;

//	float xy2 = dot(ntxy, ntxy);
//	if (!(xy2 < 1e12)) return Ng;
//
//	vec3 nt = vec3(ntxy, sqrt(max(1.0 - xy2, 1e-8)));
//	nt *= inversesqrt(dot(nt, nt));
//	nt.z = max(nt.z, 1e-4);

	float xy2 = dot(ntxy, ntxy);
	if (!(xy2 < 1.0))
	{
		if (!(xy2 > 0.0)) return Ng;
		ntxy *= inversesqrt(xy2) * 0.999;
		xy2   = 0.998001;
	}
	vec3 nt = vec3(ntxy, sqrt(1.0 - xy2));

	vec3  N    = mat3(T, B, Ng) * nt;
	float len2 = dot(N, N);
	return (len2 > 1e-12) ? N * inversesqrt(len2) : Ng;
}

#endif
