#pragma once

#include "SceneSource.h"

struct SourceAttribs
{
	std::vector<glm::vec3> normals;
	std::vector<glm::vec2> uvs;

	bool HasNormals() const noexcept { return !normals.empty(); }
	bool HasUVs()     const noexcept { return !uvs.empty(); }

	void Resize(size_t n)
	{
		if (!normals.empty()) normals.resize(n);
		if (!uvs.empty())     uvs.resize(n);
	}

	void Release()
	{
		normals.clear(); normals.shrink_to_fit();
		uvs.clear();     uvs.shrink_to_fit();
	}
};

void WeldPrimitive(SourcePrimitive& prim, SourceAttribs& attribs);
void GenerateSmoothNormals(SourcePrimitive& prim, SourceAttribs& attribs);
void GenerateTangents(SourcePrimitive& prim, const SourceAttribs& attribs);
