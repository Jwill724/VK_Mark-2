#pragma once

#include "common/ResourceTypes.h"
#include "core/AssetManager.h"

namespace Visibility {
	struct CoreSlab { uint32_t first, stride, usedCopies; };

	struct BVHNode {
		AABB box; // node bounds
		int left = -1; // child indices; -1 => leaf
		int right = -1;
		uint32_t first = 0; // start index into leafIndex[]
		uint16_t count = 0; // leaf count (0 for internal)
	};

	// Instances in VisibilityState go into one row per cullable unit,
	// that can be drawm = mesh x copy.
	// Built when copies change (multi-static slider), not per-frame.
	struct VisibilityState {
		std::vector<GPUInstance> instances; // per mesh X copy
		std::vector<AABB> worldAABBs;       // parallel to coreStatic
		std::vector<uint32_t> transformIDs; // parallel to coreStatic
		std::unordered_map<SceneID, CoreSlab> slabs;

		std::vector<uint32_t> active;    // live rows (indices into coreStatic)
		std::vector<uint32_t> leafIndex; // permutation used by BVH build
		std::vector<BVHNode> bvh;

		inline void cleanup() {
			instances.clear();
			worldAABBs.clear();
			transformIDs.clear();
			slabs.clear();

			active.clear();
			leafIndex.clear();
			bvh.clear();
		}
	};

	VisibilitySyncResult syncFromGlobalInstances(
		VisibilityState& vs,
		const std::vector<GlobalInstance>& gis, // authoritative per scene
		const std::unordered_map<SceneID, std::shared_ptr<ModelAsset>>& loaded,
		const std::vector<GPUMeshData>& meshData,
		const std::vector<glm::mat4>& transforms);

	void buildBVH(VisibilityState& vs);
	void refitBVH(const std::vector<AABB>& world,
		const std::vector<uint32_t>& leafIndex,
		std::vector<BVHNode>& nodes,
		uint32_t nIdx = 0);
	//void recomputeWorldRanges(
	//	VisibilityState& vs,
	//	const std::vector<DirtyRange>& ranges,
	//	const std::vector<GPUMeshData>& meshData,
	//	const std::vector<glm::mat4>& transforms);

	inline void applySyncResult(
		VisibilityState& vs,
		const VisibilitySyncResult& sync)
	{
		// Early out: nothing changed, BVH still valid
		if (!sync.topologyChanged && !sync.refitOnly) return;

		if (sync.topologyChanged) {
			// BVH topology changed (new or fewer nodes) -> rebuild from scratch
			buildBVH(vs);
		}
		// Topology stable but transforms moved -> cheap refit
		else if (sync.refitOnly) {
			refitBVH(vs.worldAABBs, vs.leafIndex, vs.bvh);
		}
	}

	void cullBVHCollect(
		const VisibilityState& vs,
		const Frustum& fr,
		std::vector<GPUInstance>& visibleInstances,
		std::vector<AABB>& visibleWorldAABBs);

	bool isVisible(const AABB& aabb, const Frustum& frus);
	bool boxInFrustum(const AABB& aabb, const Frustum& frus);
	AABB transformAABB(const AABB& localBox, const glm::mat4& transform);
	Frustum extractFrustum(const glm::mat4& viewproj);
	std::vector<glm::vec3> GetAABBVertices(const AABB& box);
	std::vector<glm::vec3> GetOBBVertices(const AABB& localBox, const glm::mat4& modelMatrix);
}