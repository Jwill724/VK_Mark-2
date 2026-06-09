#pragma once

#include "Bounds.h"
#include "Vertex.h"

constexpr uint32_t MESH_LOD_FLAG_FORCE_SHADOW_LOD0 = 1u << 0;
constexpr uint32_t MESH_FLAG_IS_LOD                = 1u << 1; 

struct Mesh
{
	AABB localAABB;
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

private:
	std::vector<Mesh> m_meshes;
	std::vector<MeshLODs> m_meshLODs;
};
