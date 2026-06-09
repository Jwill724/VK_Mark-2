#include "pch.h"

#include "Visibility.h"
#include "Material.h"
#include "Mesh.h"
#include "../frame/FrameResources.h"
#include "ResourceTypes.h"
#include "World.h"
#include "Scene.h"
#include "../../core/AssetUploadTypes.h"

namespace Visibility
{
	// min/max-only union
	static constexpr void GrowMinMax(AABB& dst, const AABB& src) {
		dst.vmin = glm::min(dst.vmin, src.vmin);
		dst.vmax = glm::max(dst.vmax, src.vmax);
	}

	static constexpr uint32_t TransformIDFor(const VirtualInstance& gi, uint32_t copy, uint32_t localSlot) {
		return gi.firstTransform + copy * gi.transformCount + localSlot;
	}

	// Build a GPUInstance row from the model's baked template
	static constexpr Instance MakeRow(
		const Instance& baked,
		uint32_t transformID,
		RD::InstancingMethod drawType)
	{
		Instance r{};
		r.meshID = baked.meshID;
		r.materialID = baked.materialID;
		r.transformID = transformID;
		r.passType = baked.passType;
		return r;
	}

	// === CORE FUNCTIONS ===
	uint32_t BuildMedianBVHRecursive(
		const std::vector<AABB>& world,
		std::vector<uint32_t>& leafIndex,
		std::vector<BVHNode>& nodes,
		uint32_t first,
		uint32_t count,
		uint32_t maxLeaf = 8);

	void BuildBVH(VisibilityState& vs) {
		vs.leafIndex = vs.active; // copy active indices
		vs.bvh.clear();
		if (!vs.leafIndex.empty())
			BuildMedianBVHRecursive(
				vs.worldAABBs,
				vs.leafIndex,
				vs.bvh,
				0u,
				static_cast<uint32_t>(vs.leafIndex.size()));
	}

	// === Visibility state creation, management and bvh setup. ===

	// Base bake (per scene)
	// Creates the initial rows (mesh X copies) for one scene and fills worldAABB.
	// Returns the slice [outFirst, outFirst + outCount) it wrote.
	void BakeCoreSceneMeshes(
		VisibilityState& vs,
		const VirtualInstance& gi,
		const ModelAsset& asset,
		const std::vector<Mesh>& meshData,
		const std::vector<glm::mat4>& transforms,
		uint32_t& outFirst,
		uint32_t& outCount);

	bool UpdateWorldAABBsForDynamic(
		VisibilityState& vs,
		const VirtualInstance& gi,
		const std::vector<Mesh>& meshData,
		const std::vector<glm::mat4>& transforms);

	void RebuildActive(VisibilityState& vs);
}

void Visibility::ApplySyncResult(
	VisibilityState& vs,
	const VisibilitySyncResult& sync)
{
	// Early out: nothing changed, BVH still valid
	if (!sync.topologyChanged && !sync.refitOnly) return;

	if (sync.topologyChanged)
	{
		// BVH topology changed (new or fewer nodes) -> rebuild from scratch
		BuildBVH(vs);
	}
	// Topology stable but transforms moved -> cheap refit
	else if (sync.refitOnly)
	{
		RefitBVH(vs.worldAABBs, vs.leafIndex, vs.bvh);
	}
}

// === Visibility state setup ===

VisibilitySyncResult Visibility::SyncFromGlobalInstances(
	VisibilityState& vs,
	const std::vector<VirtualInstance>& gis,
	const std::unordered_map<ModelID, std::shared_ptr<ModelAsset>>& loaded,
	const std::vector<Mesh>& meshData,
	const std::vector<glm::mat4>& transforms)
{
	VisibilitySyncResult res{};
	bool needRebuildActive = false;
	bool anyRefit = false;

	for (const auto& gi : gis)
	{
		const ModelID sid = static_cast<ModelID>(gi.sceneID);
		auto assetIt = loaded.find(sid);
		if (assetIt == loaded.end()) continue;

		const ModelAsset& asset = *assetIt->second;
		if (!asset.IsLoaded()) continue; // GPU upload not yet complete

		ASSERT(gi.perInstanceStride == static_cast<uint32_t>(asset.instances.size()));

		auto slabIt = vs.slabs.find(sid);

		if (slabIt == vs.slabs.end())
		{
			uint32_t f = 0, c = 0;
			BakeCoreSceneMeshes(vs, gi, asset, meshData, transforms, f, c);
			needRebuildActive = true;
			res.topologyChanged = true;
			continue;
		}

		anyRefit |= UpdateWorldAABBsForDynamic(vs, gi, meshData, transforms);
	}

	if (needRebuildActive) RebuildActive(vs);

	res.refitOnly = (!res.topologyChanged && anyRefit);
	return res;
}

void Visibility::BakeCoreSceneMeshes(
	VisibilityState& vs,
	const VirtualInstance& gi,
	const ModelAsset& asset,
	const std::vector<Mesh>& meshData,
	const std::vector<glm::mat4>& transforms,
	uint32_t& outFirst,
	uint32_t& outCount)
{
	const uint32_t stride = gi.perInstanceStride;
	const uint32_t copies = gi.usedCopies;

	ASSERT(stride > 0);
	ASSERT(copies >= 1);
	ASSERT(gi.transformCount > 0);
	ASSERT(gi.capacityCopies >= copies);
	ASSERT(stride == static_cast<uint32_t>(asset.instances.size()));

	const uint32_t slabTransformCount = gi.transformCount * gi.capacityCopies;
	const uint32_t slabBegin = gi.firstTransform;
	const uint32_t slabEnd   = slabBegin + slabTransformCount;
	ASSERT(slabBegin < transforms.size());
	ASSERT(slabEnd  <= transforms.size());

	outFirst = static_cast<uint32_t>(vs.instances.size());
	outCount = copies * stride;

	const size_t newSize = static_cast<size_t>(outFirst + outCount);
	vs.instances.resize(newSize);
	vs.transformIDs.resize(newSize);
	vs.worldAABBs.resize(newSize);

	uint32_t writeIndex = outFirst;
	const uint32_t usedEnd = slabBegin + gi.transformCount * copies;

	for (uint32_t copyIndex = 0; copyIndex < copies; ++copyIndex)
	{
		for (uint32_t localIndex = 0; localIndex < stride; ++localIndex, ++writeIndex)
		{
			const InstanceDesc& instDesc = asset.instances[localIndex];

			// localToNodeSlot maps primitive -> node slot in transform slab
			const uint32_t nodeSlot = asset.localToNodeSlot[localIndex];
			ASSERT(nodeSlot < gi.transformCount);

			const uint32_t transformID = TransformIDFor(gi, copyIndex, nodeSlot);
			ASSERT(transformID >= slabBegin && transformID < usedEnd);

			// meshID is already the global MeshRegistry ID
			const uint32_t meshID = instDesc.localMeshIdx;
			ASSERT(meshID < meshData.size());

			Instance row{};
			row.meshID      = meshID;
			row.materialID  = instDesc.localMaterialIdx; // already global material ID
			row.transformID = transformID;
			row.passType    = instDesc.passType;

			vs.instances[writeIndex]   = row;
			vs.transformIDs[writeIndex]= transformID;
			vs.worldAABBs[writeIndex]  = AABBtoWorldSpace(
				meshData[meshID].localAABB, transforms[transformID]);
		}
	}

	vs.slabs[static_cast<ModelID>(gi.sceneID)] = {
		.first      = outFirst,
		.stride     = stride,
		.usedCopies = copies
	};
}


bool Visibility::UpdateWorldAABBsForDynamic(
	VisibilityState& vs,
	const VirtualInstance& gi,
	const std::vector<Mesh>& meshData,
	const std::vector<glm::mat4>& transforms)
{
	if (gi.drawType != RD::InstancingMethod::DrawDynamic &&
		gi.drawType != RD::InstancingMethod::DrawMultiDynamic)
		return false;

	const auto it = vs.slabs.find(static_cast<ModelID>(gi.sceneID));
	if (it == vs.slabs.end()) return false;

	const CoreSlab& slab = it->second;
	if (slab.usedCopies == 0) return false;

	const uint32_t rowCount = slab.usedCopies * slab.stride;
	uint32_t idx = slab.first;

	for (uint32_t i = 0; i < rowCount; ++i, ++idx)
	{
		const uint32_t meshID = vs.instances[idx].meshID;
		const uint32_t tid = vs.transformIDs[idx];

		ASSERT(meshID < meshData.size());
		ASSERT(tid < transforms.size());

		vs.worldAABBs[idx] = AABBtoWorldSpace(
			meshData[meshID].localAABB, transforms[tid]);
	}

	return rowCount > 0;
}

void Visibility::RebuildActive(VisibilityState& vs)
{
	vs.active.clear();
	for (auto& [sid, slab] : vs.slabs)
	{
		const uint32_t stride = slab.stride;
		for (uint32_t c = 0; c < slab.usedCopies; ++c)
		{
			for (uint32_t local = 0; local < stride; ++local)
			{
				vs.active.push_back(slab.first + c * stride + local);
			}
		}
	}
}


// === TREE SETUP ====

// Walk the BVH, cull and emit visible rows.
void Visibility::CullBVHCollect(
	const VisibilityState& vs,
	const Frustum& frus,
	std::vector<Instance>& visibleInstances,
	std::vector<AABB>& visibleWorldAABBs,
	bool disableCulling)
{
	//visibleInstances.clear();
	//visibleWorldAABBs.clear();

	// just push all active instances(
	if (disableCulling)
	{
		// if no active data, nothing to do
		if (vs.active.empty()) return;

		visibleInstances.reserve(vs.active.size());
		visibleWorldAABBs.reserve(vs.active.size());

		for (uint32_t idx : vs.active)
		{
			visibleInstances.push_back(vs.instances[idx]);
			visibleWorldAABBs.push_back(vs.worldAABBs[idx]);
		}
		return;
	}

	if (vs.bvh.empty()) return;

	visibleInstances.reserve(vs.active.size());
	visibleWorldAABBs.reserve(vs.active.size());

	std::vector<uint32_t> stack;
	stack.reserve(128u);
	stack.push_back(0u); // root

	while (!stack.empty())
	{
		const uint32_t ni = stack.back();
		stack.pop_back();
		const BVHNode& node = vs.bvh[ni];

		if (!BoxInFrustum(node.box, frus)) continue;

		if (node.count) {
			const uint32_t first = node.first;
			const uint32_t last = first + node.count;
			for (uint32_t i = first; i < last; ++i) {
				const uint32_t idx = vs.leafIndex[i];
				const AABB& wb = vs.worldAABBs[idx];
				if (!BoxInFrustum(wb, frus)) continue;

				visibleInstances.push_back(vs.instances[idx]);
				visibleWorldAABBs.push_back(wb);
			}
		}
		else {
			stack.push_back(static_cast<uint32_t>(node.left));
			stack.push_back(static_cast<uint32_t>(node.right));
		}
	}
}

void Visibility::CullBVHCollectShadowCastersReceivers(
	uint32_t currentCascadeIndex,
	const VisibilityState& vs,
	const Frustum& frus,
	const glm::mat4& lightView,
	const glm::vec3& receiverLSMin,
	const glm::vec3& receiverLSMax,
	std::vector<Instance>& visibleInstances,
	const std::vector<uint32_t>& flagsByMaterialID)
{
	if (vs.bvh.empty()) return;

	glm::vec3 lightDirWS = glm::normalize(glm::vec3(-lightView[2]));

	auto& shadowSettings = World::GetScene().GetShadowControls();
	const float LS_EPSILON = shadowSettings.lsEpsilon;
	const float DIR_EPSILON = shadowSettings.dirEpsilon;

	std::vector<uint32_t> stack;
	stack.reserve(128u);
	stack.push_back(0u);

	while (!stack.empty())
	{
		const uint32_t ni = stack.back();
		stack.pop_back();

		const BVHNode& node = vs.bvh[ni];

		if (!BoxInFrustum(node.box, frus)) continue;

		const glm::vec3 nodeOrigin = 0.5f * (node.box.vmin + node.box.vmax);
		const glm::vec3 nodeExtent = 0.5f * (node.box.vmax - node.box.vmin);

		// Node test against loose receiver
		glm::vec3 centerLS = glm::vec3(lightView * glm::vec4(nodeOrigin, 1.0f));
		glm::vec3 extentLS = glm::mat3(
			glm::abs(lightView[0]), glm::abs(lightView[1]), glm::abs(lightView[2])) * nodeExtent;

		glm::vec3 nMin = centerLS - extentLS;
		glm::vec3 nMax = centerLS + extentLS;

		if (nMin.x > receiverLSMax.x + LS_EPSILON || nMax.x < receiverLSMin.x - LS_EPSILON) continue;
		if (nMin.y > receiverLSMax.y + LS_EPSILON || nMax.y < receiverLSMin.y - LS_EPSILON) continue;
		if (nMin.z > receiverLSMax.z + LS_EPSILON || nMax.z < receiverLSMin.z - LS_EPSILON) continue;

		if (node.count)
		{
			const uint32_t first = node.first;
			const uint32_t last  = first + node.count;

			for (uint32_t i = first; i < last; ++i)
			{
				const uint32_t idx = vs.leafIndex[i];
				const Instance& inst = vs.instances[idx];

				const uint32_t materialFlags = flagsByMaterialID[inst.materialID];

				if ((materialFlags & MATERIAL_FLAG_CASTS_SHADOWS) == 0u) continue;
				if (inst.passType == static_cast<uint32_t>(MaterialPass::Transparent)) continue;

				bool isAlphaMasked = (materialFlags & MATERIAL_FLAG_ALPHA_MASKED) != 0u;
				bool isTree        = (materialFlags & MATERIAL_FLAG_IS_TREE) != 0u;

				if (isAlphaMasked && !isTree && currentCascadeIndex >= 2) continue;
				if (isAlphaMasked && isTree && currentCascadeIndex >= 3) continue;

				const AABB& wb = vs.worldAABBs[idx];

				const glm::vec3 originWB = 0.5f * (node.box.vmin + node.box.vmax);

				float casterProj = glm::dot(lightDirWS, originWB);
				if (casterProj > DIR_EPSILON) continue;

				const glm::vec3 extentWB = 0.5f * (node.box.vmax - node.box.vmin);

				glm::vec3 casterCenterLS = glm::vec3(lightView * glm::vec4(originWB, 1.0f));
				glm::mat3 absLight = glm::mat3(
					glm::abs(lightView[0]),
					glm::abs(lightView[1]),
					glm::abs(lightView[2]));
				glm::vec3 casterExtentLS = absLight * extentWB;

				//float casterZMin = casterCenterLS.z - casterExtentLS.z;
				float casterZMax = casterCenterLS.z + casterExtentLS.z;

				//if (casterZMin > receiverLSMax.z + LS_EPSILON) continue;
				if (casterZMax < receiverLSMin.z - LS_EPSILON) continue;

				if (!BoxInFrustum(wb, frus)) continue;

				visibleInstances.push_back(inst);
			}
		}
		else
		{
			stack.push_back(static_cast<uint32_t>(node.left));
			stack.push_back(static_cast<uint32_t>(node.right));
		}
	}
}

void Visibility::CullBVHCollectShadowCasters(
	const VisibilityState& vs,
	const Frustum& frus,
	std::vector<Instance>& visibleInstances,
	const std::vector<uint32_t>& flagsByMaterialID,
	bool allowAlphaMasked)
{
	if (vs.bvh.empty()) return;

	std::vector<uint32_t> stack;
	stack.reserve(128u);
	stack.push_back(0u); // root

	while (!stack.empty())
	{
		const uint32_t ni = stack.back();
		stack.pop_back();
		const BVHNode& node = vs.bvh[ni];

		if (!BoxInFrustum(node.box, frus)) continue;

		if (node.count)
		{
			const uint32_t first = node.first;
			const uint32_t last = first + node.count;
			for (uint32_t i = first; i < last; ++i)
			{
				const uint32_t idx = vs.leafIndex[i];
				const Instance& inst = vs.instances[idx];

				const uint32_t materialFlags = flagsByMaterialID[inst.materialID];

				// Don't want transparency for shadows
				if ((materialFlags & MATERIAL_FLAG_CASTS_SHADOWS) == 0u) continue;
				if (inst.passType == static_cast<uint32_t>(MaterialPass::Transparent)) continue;

				// Don’t include masked in far cascades
				if (!allowAlphaMasked && (materialFlags & MATERIAL_FLAG_ALPHA_MASKED) != 0u) continue;

				const AABB& wb = vs.worldAABBs[idx];
				if (!BoxInFrustum(wb, frus)) continue;

				visibleInstances.push_back(inst);
			}
		}
		else
		{
			stack.push_back(static_cast<uint32_t>(node.left));
			stack.push_back(static_cast<uint32_t>(node.right));
		}
	}
}

uint32_t Visibility::BuildMedianBVHRecursive(
	const std::vector<AABB>& world,
	std::vector<uint32_t>& leafIndex,
	std::vector<BVHNode>& nodes,
	uint32_t first,
	uint32_t count,
	uint32_t maxLeaf)
{
	BVHNode node{};
	// bounds and centroid bounds
	AABB nodeB{};
	nodeB.vmin = glm::vec3(1e30f);
	nodeB.vmax = glm::vec3(-1e30f);

	glm::vec3 cmin(1e30f), cmax(-1e30f);
	for (uint32_t i = 0; i < count; ++i)
	{
		const AABB& a = world[leafIndex[first + i]];
		const glm::vec3 origin = 0.5f * (a.vmin + a.vmax);
		if (i == 0) {
			nodeB = a; // copies vmin/vmax
			cmin = cmax = origin;
		}
		else
		{
			GrowMinMax(nodeB, a); // min/max only
			cmin = glm::min(cmin, origin);
			cmax = glm::max(cmax, origin);
		}
	}

	const uint32_t idx = static_cast<uint32_t>(nodes.size());
	nodes.push_back(node);
	nodes[idx].box = nodeB;

	// leaf?
	if (count <= maxLeaf || glm::all(glm::lessThanEqual(cmax - cmin, glm::vec3(1e-6f))))
	{
		nodes[idx].first = first;
		nodes[idx].count = static_cast<uint16_t>(count);
		return idx;
	}

	// split axis by largest centroid m_extent
	glm::vec3 cExt = cmax - cmin;
	int axis = (cExt.x > cExt.y && cExt.x > cExt.z) ? 0 : (cExt.y > cExt.z ? 1 : 2);

	// median partition on chosen axis
	const uint32_t mid = first + count / 2;
	std::nth_element(leafIndex.begin() + first, leafIndex.begin() + mid, leafIndex.begin() + first + count,
		[&](uint32_t ia, uint32_t ib) {
			const glm::vec3 worldAOrigin = 0.5f * (world[ia].vmin + world[ia].vmax);
			const glm::vec3 worldBOrigin = 0.5f * (world[ib].vmin + world[ib].vmax);
			return worldAOrigin[axis] < worldBOrigin[axis];
		});

	// recurse
	uint32_t L = BuildMedianBVHRecursive(world, leafIndex, nodes, first, mid - first, maxLeaf);
	uint32_t R = BuildMedianBVHRecursive(world, leafIndex, nodes, mid, first + count - mid, maxLeaf);

	nodes[idx].left = static_cast<int>(L);
	nodes[idx].right = static_cast<int>(R);
	return idx;
}

void Visibility::RefitBVH(
	const std::vector<AABB>& world,
	const std::vector<uint32_t>& leafIndex,
	std::vector<BVHNode>& nodes,
	uint32_t nIdx)
{
	BVHNode& n = nodes[nIdx];
	if (n.count)
	{
		AABB b{};
		b.vmin = glm::vec3(1e30f);
		b.vmax = glm::vec3(-1e30f);

		for (uint32_t i = 0; i < n.count; ++i) {
			const AABB& w = world[leafIndex[n.first + i]];
			if (i == 0) b = w; else GrowMinMax(b, w);
		}
		n.box = b;
		return;
	}
	RefitBVH(world, leafIndex, nodes, static_cast<uint32_t>(n.left));
	RefitBVH(world, leafIndex, nodes, static_cast<uint32_t>(n.right));

	const BVHNode& L = nodes[n.left];
	const BVHNode& R = nodes[n.right];

	AABB b{};
	b.vmin = glm::min(L.box.vmin, R.box.vmin);
	b.vmax = glm::max(L.box.vmax, R.box.vmax);
	n.box = b;
}

// === CORE CULL FUNCTIONS AND WORLD AABB/FRUSTUM SETUP ===

// CPU Sided culling
// Frustum culling method https://iquilezles.org/articles/frustumcorrect/
bool Visibility::BoxInFrustum(const AABB& box, const Frustum& fru)
{
	const glm::vec3 center = 0.5f * (box.vmin + box.vmax);
	const glm::vec3 extents = 0.5f * (box.vmax - box.vmin);
	const float sphereRadius = glm::length(extents);

	const float minSafeRadius = sphereRadius * 0.01f;
	const float safeRadius = glm::max(sphereRadius, minSafeRadius);

	//For each plane in the frustum
	for (int i = 0; i < 6; ++i)
	{
		glm::vec3 normal = glm::vec3(fru.planes[i]);
		float d = fru.planes[i].w;

		float dist = glm::dot(normal, center) + d;

		if (dist < -safeRadius) return false;

		float r =
			extents.x * abs(normal.x) +
			extents.y * abs(normal.y) +
			extents.z * abs(normal.z);

		if (dist + r < 0.0f) return false;
	}

	return true;
}
