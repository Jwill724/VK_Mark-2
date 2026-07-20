#pragma once

#include "Bounds.h"
#include "Vertex.h"

inline constexpr uint32_t MESH_FLAG_IS_LOD                = 1u << 0;
inline constexpr uint32_t MESH_FLAG_GOOD_OCCLUDEE         = 1u << 1;
inline constexpr uint32_t MESH_LOD_FLAG_FORCE_SHADOW_LOD0 = 1u << 2;

struct Mesh
{
	AABB localAABB;
	float localBoundingRadius = 0.0f;
	uint32_t firstIndex = UINT32_MAX;
	uint32_t indexCount = UINT32_MAX;
	uint32_t vertexOffset = UINT32_MAX;
	uint32_t vertexCount = UINT32_MAX;
	uint32_t shadowFirstIndex = UINT32_MAX;
	uint32_t shadowIndexCount = UINT32_MAX;
};

struct MeshLODs
{
	uint32_t lod0 = UINT32_MAX;
	uint32_t lod1 = UINT32_MAX;
	uint32_t lod2 = UINT32_MAX;
	uint32_t lod3 = UINT32_MAX;

	uint32_t shadowLod0 = UINT32_MAX;
	uint32_t shadowLod1 = UINT32_MAX;
	uint32_t shadowLod2 = UINT32_MAX;

	uint32_t flags = 0;
};

class MeshRegistry
{
public:
	// Add all meshes in before calling this
	void ResizeMeshLods()
	{
		if (m_meshLODs.size() < m_meshes.size())
		{
			m_meshLODs.resize(m_meshes.size());
		}
	}

	uint32_t GetMeshCount()
	{
		return static_cast<uint32_t>(m_meshes.size());
	}

	const std::vector<Mesh>& GetMeshes() const { return m_meshes; }
	//std::vector<Mesh>& GetMeshesMutable() { return m_meshes; }
	const std::vector<MeshLODs>& GetLods() const { return m_meshLODs; }
	std::vector<MeshLODs>& GetLodsMutable() { return m_meshLODs; }

	std::vector<uint32_t> ExtractAllMeshIDs() const
	{
		std::vector<uint32_t> ids;
		ids.reserve(m_meshes.size());

		for (uint32_t id = 0; id < m_meshes.size(); ++id)
		{
			ids.push_back(id);
		}

		return ids;
	}

	uint32_t RegisterMesh(const Mesh& data)
	{
		uint32_t id = static_cast<uint32_t>(m_meshes.size());

		m_meshes.push_back(data);

		MeshLODs lods{};
		lods.lod0 = id;
		lods.lod1 = id;
		lods.lod2 = id;
		lods.lod3 = id;
		m_meshLODs.push_back(lods);

		return id;
	}

	// Catches tiny meshes for light leak potential
	static bool IsThinMeshForShadows(const Mesh& mesh)
	{
		const glm::vec3 extent = (mesh.localAABB.vmax - mesh.localAABB.vmin);
		//const glm::vec3 extent = (0.5f + (mesh.localAABB.vmax - mesh.localAABB.vmin)) * 2.0f; // full size
		float minAxis = std::min(extent.x, std::min(extent.y, extent.z));
		float maxAxis = std::max(extent.x, std::max(extent.y, extent.z));

		if (maxAxis <= 0.0001f) return true;

		float thinRatio = minAxis / maxAxis;
		return thinRatio < 0.03f;
	}

	static uint32_t GetShadowSlotForCascade(const MeshLODs& lods, uint32_t cascade)
	{
		if (lods.flags & MESH_LOD_FLAG_FORCE_SHADOW_LOD0) return 0u;
		if (cascade == 0 || cascade == 1) return 0u;
		if (cascade == 2) return 1u;
		return 2u;
	}

	static uint32_t ApplyFoliageBias(uint32_t baseSlot, uint32_t cascade)
	{
		if (cascade == 0u) return 0u;
		if (cascade == 1u && baseSlot > 0u) return baseSlot - 1u;
		return baseSlot;
	}

	// Computes a tight local-space bounding sphere for a mesh's vertices.
	static float ComputeLocalBoundingRadius(
		const Vertex* verts,
		uint32_t      vertexCount,
		const glm::vec3& aabbMin,
		const glm::vec3& aabbMax)
	{
		if (vertexCount == 0) return 0.0f;

		const glm::vec3 center = 0.5f * (aabbMin + aabbMax);
		float radiusSq = 0.0f;

		for (uint32_t i = 0; i < vertexCount; ++i)
		{
			const glm::vec3 d = verts[i].position - center;
			radiusSq = std::max(radiusSq, glm::dot(d, d));
		}

		float radius = std::sqrt(radiusSq);

		// Find point furthest from an arbitrary vertex (v0), then find point
		// furthest from that — approximates the diameter endpoints.
		glm::vec3 p0 = verts[0].position;
		glm::vec3 pA = p0;
		float maxDSq = 0.0f;

		for (uint32_t i = 1; i < vertexCount; ++i)
		{
			const glm::vec3 d = verts[i].position - p0;
			const float dSq = glm::dot(d, d);
			if (dSq > maxDSq) { maxDSq = dSq; pA = verts[i].position; }
		}

		glm::vec3 pB = pA;
		maxDSq = 0.0f;
		for (uint32_t i = 0; i < vertexCount; ++i)
		{
			const glm::vec3 d = verts[i].position - pA;
			const float dSq = glm::dot(d, d);
			if (dSq > maxDSq) { maxDSq = dSq; pB = verts[i].position; }
		}

		glm::vec3 ritterCenter = 0.5f * (pA + pB);
		float ritterRadius = 0.5f * glm::length(pB - pA);

		// Grow the Ritter sphere to enclose any outliers it missed
		for (uint32_t i = 0; i < vertexCount; ++i)
		{
			const glm::vec3 d = verts[i].position - ritterCenter;
			const float dist = glm::length(d);
			if (dist > ritterRadius)
			{
				const float newRadius = 0.5f * (ritterRadius + dist);
				const float k = (newRadius - ritterRadius) / dist;
				ritterCenter += d * k;
				ritterRadius = newRadius;
			}
		}

		return std::min(radius, ritterRadius);
	}

private:
	std::vector<Mesh> m_meshes;
	std::vector<MeshLODs> m_meshLODs;
};
