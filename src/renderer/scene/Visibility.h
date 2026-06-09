#pragma once

#include "Bounds.h"
#include <unordered_map>

struct ModelAsset;
struct VisibilityState;
struct VirtualInstance;
struct Mesh;
struct BVHNode;
struct Instance;

enum class ModelID;
struct VisibilitySyncResult;

namespace Visibility
{
	VisibilitySyncResult SyncFromGlobalInstances(
		VisibilityState& vs,
		const std::vector<VirtualInstance>& gis, // authoritative per scene
		const std::unordered_map<ModelID, std::shared_ptr<ModelAsset>>& loaded,
		const std::vector<Mesh>& meshData,
		const std::vector<glm::mat4>& transforms);

	void BuildBVH(VisibilityState& vs);
	void RefitBVH(const std::vector<AABB>& world,
		const std::vector<uint32_t>& leafIndex,
		std::vector<BVHNode>& nodes,
		uint32_t nIdx = 0);

	void ApplySyncResult(VisibilityState& vs, const VisibilitySyncResult& sync);

	void CullBVHCollect(
		const VisibilityState& vs,
		const Frustum& fr,
		std::vector<Instance>& visibleInstances,
		std::vector<AABB>& visibleWorldAABBs,
		bool disableCulling = false);

	void CullBVHCollectShadowCastersReceivers(
		uint32_t cascadeIndex,
		const VisibilityState& vs,
		const Frustum& cascadeFrustum,
		const glm::mat4& lightView,
		const glm::vec3& receiverLSMin,
		const glm::vec3& receiverLSMax,
		std::vector<Instance>& out,
		const std::vector<uint32_t>& flags);

	void CullBVHCollectShadowCasters(
		const VisibilityState& vs,
		const Frustum& frus,
		std::vector<Instance>& visibleInstances,
		const std::vector<uint32_t>& flagsByMaterialID,
		bool allowAlphaMasked);

	bool BoxInFrustum(const AABB& aabb, const Frustum& frus);
}
