#include "pch.h"

#include "SourceGeometry.h"

void WeldPrimitive(SourcePrimitive& prim, SourceAttribs& attribs)
{
	const size_t cornerCount = prim.vertices.size();
	if (cornerCount < 3) return;

	std::vector<uint32_t> remap(cornerCount);
	const size_t uniqueCount = meshopt_generateVertexRemap(
		remap.data(), nullptr, cornerCount,
		prim.vertices.data(), cornerCount, sizeof(Vertex));

	std::vector<uint32_t> indices(cornerCount);
	meshopt_remapIndexBuffer(indices.data(), nullptr, cornerCount, remap.data());

	std::vector<Vertex> welded(uniqueCount);
	meshopt_remapVertexBuffer(welded.data(), prim.vertices.data(),
		cornerCount, sizeof(Vertex), remap.data());

	if (attribs.HasNormals())
	{
		std::vector<glm::vec3> n(uniqueCount);
		meshopt_remapVertexBuffer(n.data(), attribs.normals.data(),
			cornerCount, sizeof(glm::vec3), remap.data());
		attribs.normals = std::move(n);
	}

	if (attribs.HasUVs())
	{
		std::vector<glm::vec2> u(uniqueCount);
		meshopt_remapVertexBuffer(u.data(), attribs.uvs.data(),
			cornerCount, sizeof(glm::vec2), remap.data());
		attribs.uvs = std::move(u);
	}

	prim.vertices = std::move(welded);
	prim.indices = std::move(indices);
}

void GenerateSmoothNormals(SourcePrimitive& prim, SourceAttribs& attribs)
{
	const size_t vCount = prim.vertices.size();
	if (vCount == 0 || prim.indices.size() < 3) return;

	attribs.normals.assign(vCount, glm::vec3(0.0f));

	for (size_t i = 0; i + 2 < prim.indices.size(); i += 3)
	{
		const uint32_t i0 = prim.indices[i + 0];
		const uint32_t i1 = prim.indices[i + 1];
		const uint32_t i2 = prim.indices[i + 2];

		const glm::vec3 n = glm::cross(
			prim.vertices[i1].position - prim.vertices[i0].position,
			prim.vertices[i2].position - prim.vertices[i0].position);

		attribs.normals[i0] += n;
		attribs.normals[i1] += n;
		attribs.normals[i2] += n;
	}

	for (size_t i = 0; i < vCount; ++i)
	{
		const float len2 = glm::dot(attribs.normals[i], attribs.normals[i]);
		attribs.normals[i] = (len2 > 1e-20f)
			? attribs.normals[i] * glm::inversesqrt(len2)
			: glm::vec3(0, 1, 0);

		EncodeOctahedral_Normal(prim.vertices[i], attribs.normals[i]);
	}
}

void GenerateTangents(SourcePrimitive& prim, const SourceAttribs& attribs)
{
	const size_t vCount = prim.vertices.size();
	if (vCount == 0 || prim.indices.size() < 3)  return;
	if (!attribs.HasNormals() || !attribs.HasUVs()) return;
	if (attribs.normals.size() != vCount || attribs.uvs.size() != vCount) return;

	std::vector<glm::vec3> tan(vCount, glm::vec3(0.0f));
	std::vector<glm::vec3> bit(vCount, glm::vec3(0.0f));

	for (size_t i = 0; i + 2 < prim.indices.size(); i += 3)
	{
		const uint32_t i0 = prim.indices[i + 0];
		const uint32_t i1 = prim.indices[i + 1];
		const uint32_t i2 = prim.indices[i + 2];

		const glm::vec3 e1 = prim.vertices[i1].position - prim.vertices[i0].position;
		const glm::vec3 e2 = prim.vertices[i2].position - prim.vertices[i0].position;

		const glm::vec2 d1 = attribs.uvs[i1] - attribs.uvs[i0];
		const glm::vec2 d2 = attribs.uvs[i2] - attribs.uvs[i0];

		const float det = d1.x * d2.y - d2.x * d1.y;
		if (std::abs(det) < 1e-12f) continue;
		const float r = 1.0f / det;

		const glm::vec3 t = (e1 * d2.y - e2 * d1.y) * r;
		const glm::vec3 b = (e2 * d1.x - e1 * d2.x) * r;

		tan[i0] += t; tan[i1] += t; tan[i2] += t;
		bit[i0] += b; bit[i1] += b; bit[i2] += b;
	}

	for (size_t i = 0; i < vCount; ++i)
	{
		const glm::vec3 n = attribs.normals[i];
		glm::vec3 t = tan[i] - n * glm::dot(n, tan[i]);

		if (glm::dot(t, t) < 1e-16f)
		{
			const glm::vec3 axis = (std::abs(n.x) < 0.9f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
			t = glm::normalize(glm::cross(axis, n));
		}
		else
		{
			t = glm::normalize(t);
		}

		const float sign = (glm::dot(glm::cross(n, t), bit[i]) < 0.0f) ? -1.0f : 1.0f;
		EncodeOctahedral_Tangent(prim.vertices[i], glm::vec4(t, sign));
	}
}
