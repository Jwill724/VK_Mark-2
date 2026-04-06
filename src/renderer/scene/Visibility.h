#pragma once

#include "common/ResourceTypes.h"
#include "core/AssetManager.h"

namespace Visibility {
	struct CoreSlab {
		uint32_t first = 0u;
		uint32_t stride = 0u;
		uint32_t usedCopies = 0u;
	};

	AllocatedBuffer& getRenderables();
	AllocatedBuffer& getWorldAABBs();
	AllocatedBuffer& getTransformIDs();

	struct BVHNode {
		AABB box; // node bounds
		glm::vec3 extent;
		glm::vec3 origin;
		float sphereRadius;
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

	void applySyncResult(VisibilityState& vs, const VisibilitySyncResult& sync);

	void cullBVHCollect(
		const VisibilityState& vs,
		const Frustum& fr,
		std::vector<GPUInstance>& visibleInstances,
		std::vector<AABB>& visibleWorldAABBs,
		bool disableCulling = false);

	void cullBVHCollectShadowCastersReceivers(
		uint32_t cascadeIndex,
		const VisibilityState& vs,
		const Frustum& cascadeFrustum,
		const glm::mat4& lightView,
		const glm::vec3& receiverLSMin,
		const glm::vec3& receiverLSMax,
		std::vector<GPUInstance>& out,
		const std::vector<uint32_t>& flags);

	void cullBVHCollectShadowCasters(
		const VisibilityState& vs,
		const Frustum& frus,
		std::vector<GPUInstance>& visibleInstances,
		const std::vector<uint32_t>& flagsByMaterialID,
		bool allowAlphaMasked);

	bool boxInFrustum(const AABB& aabb, const Frustum& frus, bool useCorners = false);
}
