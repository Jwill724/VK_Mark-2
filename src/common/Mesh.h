#pragma once

#include "Bounds.h"
#include "Vertex.h"
#include <meshoptimizer.h>

constexpr uint32_t MESH_LOD_FLAG_FORCE_SHADOW_LOD0 = 1u << 0;

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

// In mesh setup all model vertices/indices are collected
// to be batched in one upload
struct UploadMeshContext
{
	std::vector<uint32_t> globalIndices;
	std::vector<Vertex> globalVertices;
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
	const std::vector<MeshLODs>& GetLods() const { return m_meshLODs; }

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

	uint32_t BuildLOD(
		Mesh& newMesh,
		float ratio,
		float error,
		uint32_t vertexCount,
		uint32_t indexCount,
		std::vector<uint32_t>& indices,
		std::vector<uint32_t>& lodIndex,
		std::vector<uint32_t>& baseIndexCopy,
		const float simplifyScale,
		const float* positionPtr)
	{
		size_t targetIndexCount = static_cast<size_t>(static_cast<float>(indexCount) * ratio);
		targetIndexCount = (targetIndexCount / 3u) * 3u;
		targetIndexCount = std::max<size_t>(3u, targetIndexCount);

		if (targetIndexCount >= static_cast<size_t>(indexCount)) return UINT32_MAX;

		lodIndex.resize(static_cast<size_t>(indexCount));

		const float targetError = error * simplifyScale;

		const size_t lodIndexCount = meshopt_simplify(
			lodIndex.data(),
			baseIndexCopy.data(),
			static_cast<size_t>(indexCount),
			positionPtr,
			static_cast<size_t>(vertexCount),
			sizeof(Vertex),
			targetIndexCount,
			targetError);

		if (lodIndexCount < 3u || (lodIndexCount % 3u) != 0u) return UINT32_MAX;

		lodIndex.resize(lodIndexCount);

		meshopt_optimizeVertexCache(
			lodIndex.data(),
			lodIndex.data(),
			lodIndexCount,
			static_cast<size_t>(vertexCount));

		meshopt_optimizeOverdraw(
			lodIndex.data(),
			lodIndex.data(),
			lodIndexCount,
			positionPtr,
			static_cast<size_t>(vertexCount),
			sizeof(Vertex),
			1.05f);

	#ifndef NDEBUG
		uint32_t lodMaxIndex = 0;
		for (size_t k = 0; k < lodIndexCount; ++k) {
			lodMaxIndex = std::max(lodMaxIndex, lodIndex[k]);
		}
		ASSERT(lodMaxIndex < vertexCount && "[meshopt] LOD indices out of bounds.");
	#endif

		const size_t lodIdxOff = indices.size();
		indices.insert(indices.end(), lodIndex.begin(), lodIndex.end());

		Mesh lodMesh = newMesh;
		lodMesh.firstIndex = static_cast<uint32_t>(lodIdxOff);
		lodMesh.indexCount = static_cast<uint32_t>(lodIndexCount);

		return RegisterMesh(lodMesh);
	};

	// Catches tiny meshes for light leak potential
	static bool IsThinMeshForShadows(const Mesh& mesh)
	{
		glm::vec3 extent = (0.5f + (mesh.localAABB.vmax - mesh.localAABB.vmin)) * 2.0f; // full size

		float minAxis = std::min(extent.x, std::min(extent.y, extent.z));
		float maxAxis = std::max(extent.x, std::max(extent.y, extent.z));

		if (maxAxis <= 0.0001f) return true;

		float thinRatio = minAxis / maxAxis;
		return thinRatio < 0.03f;
	}

	// Maintain high quality meshes within acceptable distances to maintain stability
	static uint32_t GetShadowMeshIDForCascade(
		const MeshLODs& lods,
		uint32_t cascadeIndex)
	{
		if ((lods.flags & MESH_LOD_FLAG_FORCE_SHADOW_LOD0) != 0u) return lods.shadowLod0;

		if (cascadeIndex == 0 || cascadeIndex == 1) return lods.shadowLod0;
		if (cascadeIndex == 2) return lods.shadowLod1;
		return lods.shadowLod2;
	}

	static uint32_t ShadowSlotToMeshID(const MeshLODs& lods, uint32_t slot)
	{
		if (slot == 0u) return lods.shadowLod0;
		if (slot == 1u) return lods.shadowLod1;
		return lods.shadowLod2;
	}

	static uint32_t ApplyFoliageBias(uint32_t baseSlot, uint32_t cascadeIndex)
	{
		if (cascadeIndex == 0u) return 0u; // force best

		if (cascadeIndex == 1u)
		{
			// 1 step more detailed than the base rule
			if (baseSlot > 0u) return baseSlot - 1u;
			return 0u;
		}

		return baseSlot;
	}
private:
	std::vector<Mesh> m_meshes;
	std::vector<MeshLODs> m_meshLODs;
};
