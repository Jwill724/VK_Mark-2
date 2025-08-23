#include "pch.h"

#include "Visibility.h"
#include "renderer/gpu/PipelineManager.h"

namespace Visibility {
	// === HELPERS ===
	static glm::vec3 centerOf(const AABB& a) { return a.origin; }

	// min/max-only union
	static inline void growMinMax(AABB& dst, const AABB& src) {
		dst.vmin = glm::min(dst.vmin, src.vmin);
		dst.vmax = glm::max(dst.vmax, src.vmax);
	}

	// finalize origin/extent/sphere once, after unions
	static inline void finalizeFromMinMax(AABB& b) {
		b.origin = 0.5f * (b.vmin + b.vmax);
		b.extent = 0.5f * (b.vmax - b.vmin);
		b.sphereRadius = glm::length(b.extent);
	}

	static inline uint32_t transformIDFor(const GlobalInstance& gi, uint32_t copy, uint32_t localSlot) {
		return gi.firstTransform + copy * gi.transformCount + localSlot;
	}

	// Build a GPUInstance row from the model's baked template
	static inline GPUInstance makeRow(
		const GPUInstance& baked,
		uint32_t transformID,
		DrawType drawType)
	{
		GPUInstance r{};
		r.meshID = baked.meshID;
		r.materialID = baked.materialID;
		r.transformID = transformID;
		r.drawType = static_cast<uint32_t>(drawType);
		r.passType = baked.passType;
		return r;
	}

	// === CORE FUNCTIONS ===
	static uint32_t buildMedianBVHRecursive(
		const std::vector<AABB>& world,
		std::vector<uint32_t>& leafIndex,
		std::vector<BVHNode>& nodes,
		uint32_t first,
		uint32_t count,
		uint32_t maxLeaf = 8);

	static void refitBVH(
		const std::vector<AABB>& world,
		const std::vector<uint32_t>& leafIndex,
		std::vector<BVHNode>& nodes,
		uint32_t nIdx = 0);

	void buildBVH(VisibilityState& vs) {
		vs.leafIndex = vs.active; // copy active indices (will be permuted by builder)
		vs.bvh.clear();
		if (!vs.leafIndex.empty())
			buildMedianBVHRecursive(
				vs.worldAABBs,
				vs.leafIndex,
				vs.bvh,
				0u,
				static_cast<uint32_t>(vs.leafIndex.size()));
	}

	void refitBVH(VisibilityState& vs) {
		if (!vs.bvh.empty())
			refitBVH(vs.worldAABBs, vs.leafIndex, vs.bvh);
	}

	// === Visibility state creation, management and bvh setup. ===

	// Base bake (per scene)
	// Creates the initial rows (mesh X copies) for one scene and fills worldAABB.
	// Returns the slice [outFirst, outFirst + outCount) it wrote.
	static void bakeCoreSceneMeshes(
		VisibilityState& vs,
		const GlobalInstance& gi,
		const ModelAsset& asset,
		const std::vector<GPUMeshData>& meshData,
		const std::vector<glm::mat4>& transforms,
		uint32_t& outFirst,
		uint32_t& outCount);

	// Recompute world AABBs for a contiguous slice
	void recomputeWorldRanges(
		VisibilityState& vs,
		const std::vector<DirtyRange>& ranges,
		const std::vector<GPUMeshData>& meshData,
		const std::vector<glm::mat4>& transforms);

	// Append ONLY newly-realized copies for a scene (multi draw slider increased).
	// Fills core rows, transformIDs, worldAABBs for the new range and activates them.
	// Returns the appended slice [outFirst, outFirst + outCount).
	static void appendSceneCopies(
		VisibilityState& vs,
		const GlobalInstance& gi,
		uint32_t oldCopies,
		const ModelAsset& asset,
		const std::vector<GPUMeshData>& meshData,
		const std::vector<glm::mat4>& transforms,
		uint32_t& outFirst,
		uint32_t& outCount);

	// Lazy shrink (slider decreased). No memory reclamation; just reduce usedCopies
	// and rebuild the 'active' list. Call buildBVH() after this (topology changed).
	static void shrinkSceneCopiesLazy(VisibilityState& vs, SceneID sid, uint32_t newCopies);

	// Transform slab moved (firstTransform changed) but copy count is the same.
	// Rewrites the scene's slice with new transformIDs and worldAABBs. Then refitBVH().
	static void rewriteSceneSlice(
		VisibilityState& vs,
		const GlobalInstance& gi,
		const ModelAsset& asset,
		const std::vector<GPUMeshData>& meshData,
		const std::vector<glm::mat4>& transforms);

	static void rebuildActive(VisibilityState& vs);
}

void Visibility::bakeCoreSceneMeshes(
	VisibilityState& vs,
	const GlobalInstance& gi,
	const ModelAsset& asset,
	const std::vector<GPUMeshData>& meshData,
	const std::vector<glm::mat4>& transforms,
	uint32_t& outFirst,
	uint32_t& outCount)
{
	const uint32_t stride = gi.perInstanceStride; // == bakedInstances.size()
	const uint32_t copies = gi.usedCopies;        // includes base
	ASSERT(stride == asset.runtime.bakedInstances.size());
	ASSERT(copies >= 1);

	outFirst = static_cast<uint32_t>(vs.instances.size());
	outCount = copies * stride;

	const size_t newSize = static_cast<size_t>(outFirst + outCount);
	vs.instances.resize(newSize);
	vs.transformIDs.resize(newSize);
	vs.worldAABBs.resize(newSize);

	uint32_t w = outFirst;
	for (uint32_t c = 0; c < copies; ++c) {
		for (uint32_t local = 0; local < stride; ++local, ++w) {
			const GPUInstance& baked = *asset.runtime.bakedInstances[local];

			const uint32_t nodeSlot = static_cast<uint32_t>(asset.runtime.localToNodeSlot[local]);
			const uint32_t tid = transformIDFor(gi, c, nodeSlot);

			vs.instances[w] = makeRow(baked, tid, gi.drawType);
			vs.transformIDs[w] = tid;

			const uint32_t meshID = baked.meshID;
			ASSERT(meshID < meshData.size());
			ASSERT(tid < transforms.size());
			ASSERT(tid >= gi.firstTransform && tid < gi.firstTransform + gi.transformCount * gi.usedCopies);
			vs.worldAABBs[w] = transformAABB(meshData[meshID].localAABB, transforms[tid]);
		}
	}

	vs.slabs[static_cast<SceneID>(gi.sceneID)] = { outFirst, stride, copies };
}

uint32_t Visibility::buildMedianBVHRecursive(
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
	for (uint32_t i = 0; i < count; ++i) {
		const AABB& a = world[leafIndex[first + i]];
		if (i == 0) {
			nodeB = a; // copies vmin/vmax/origin/extent/radius;
			cmin = cmax = centerOf(a);
		}
		else {
			growMinMax(nodeB, a); // min/max only
			cmin = glm::min(cmin, centerOf(a));
			cmax = glm::max(cmax, centerOf(a));
		}
	}
	finalizeFromMinMax(nodeB); // compute origin/extent/radius once

	const uint32_t idx = static_cast<uint32_t>(nodes.size());
	nodes.push_back(node);
	nodes[idx].box = nodeB;

	// leaf?
	if (count <= maxLeaf || glm::all(glm::lessThanEqual(cmax - cmin, glm::vec3(1e-6f)))) {
		nodes[idx].first = first;
		nodes[idx].count = static_cast<uint16_t>(count);
		return idx;
	}

	// split axis by largest centroid extent
	glm::vec3 cExt = cmax - cmin;
	int axis = (cExt.x > cExt.y && cExt.x > cExt.z) ? 0 : (cExt.y > cExt.z ? 1 : 2);

	// median partition on chosen axis
	const uint32_t mid = first + count / 2;
	std::nth_element(leafIndex.begin() + first, leafIndex.begin() + mid, leafIndex.begin() + first + count,
		[&](uint32_t ia, uint32_t ib) {
			return centerOf(world[ia])[axis] < centerOf(world[ib])[axis];
		});

	// recurse
	uint32_t L = buildMedianBVHRecursive(world, leafIndex, nodes, first, mid - first, maxLeaf);
	uint32_t R = buildMedianBVHRecursive(world, leafIndex, nodes, mid, first + count - mid, maxLeaf);

	nodes[idx].left = static_cast<int>(L);
	nodes[idx].right = static_cast<int>(R);
	return idx;
}

void Visibility::refitBVH(
	const std::vector<AABB>& world,
	const std::vector<uint32_t>& leafIndex,
	std::vector<BVHNode>& nodes,
	uint32_t nIdx)
{
	BVHNode& n = nodes[nIdx];
	if (n.count) {
		AABB b{};
		b.vmin = glm::vec3(1e30f);
		b.vmax = glm::vec3(-1e30f);

		for (uint32_t i = 0; i < n.count; ++i) {
			const AABB& w = world[leafIndex[n.first + i]];
			if (i == 0) b = w; else growMinMax(b, w);
		}
		finalizeFromMinMax(b);
		n.box = b;
		return;
	}
	refitBVH(world, leafIndex, nodes, static_cast<uint32_t>(n.left));
	refitBVH(world, leafIndex, nodes, static_cast<uint32_t>(n.right));

	const BVHNode& L = nodes[n.left];
	const BVHNode& R = nodes[n.right];

	AABB b{};
	b.vmin = glm::min(L.box.vmin, R.box.vmin);
	b.vmax = glm::max(L.box.vmax, R.box.vmax);
	finalizeFromMinMax(b);
	n.box = b;
}

void Visibility::rebuildActive(VisibilityState& vs) {
	vs.active.clear();
	for (auto& [sid, slab] : vs.slabs) {
		const uint32_t stride = slab.stride;
		for (uint32_t c = 0; c < slab.usedCopies; ++c) {
			for (uint32_t local = 0; local < stride; ++local) {
				vs.active.push_back(slab.first + c * stride + local);
			}
		}
	}
}


VisibilitySyncResult Visibility::syncFromGlobalInstances(
	VisibilityState& vs,
	const std::vector<GlobalInstance>& gis,
	const std::unordered_map<SceneID, std::shared_ptr<ModelAsset>>& loaded,
	const std::vector<GPUMeshData>& meshData,
	const std::vector<glm::mat4>& transforms)
{
	VisibilitySyncResult res{};
	bool needRebuildActive = false;

	for (const GlobalInstance& gi : gis) {
		// only care about static/multi-static for this path
		if (gi.drawType != DrawType::DrawStatic && gi.drawType != DrawType::DrawMultiStatic) continue;

		const SceneID sid = static_cast<SceneID>(gi.sceneID);
		const auto assetIt = loaded.find(sid);
		if (assetIt == loaded.end()) continue;
		const ModelAsset& asset = *assetIt->second;
		const uint32_t stride = gi.perInstanceStride;
		ASSERT(stride == asset.runtime.bakedInstances.size());

		auto slabIt = vs.slabs.find(sid);

		// First-time bake for this scene
		if (slabIt == vs.slabs.end()) {
			uint32_t f = 0, c = 0;
			bakeCoreSceneMeshes(vs, gi, asset, meshData, transforms, f, c);
			needRebuildActive = true;
			res.topologyChanged = true;
			//res.dirtyTransformRanges.push_back({ f, c }); // rows that were just wrote
			continue;
		}

		CoreSlab& slab = slabIt->second;
		// Copies changed?
		if (gi.usedCopies > slab.usedCopies) {
			const uint32_t oldCopies = slab.usedCopies;
			uint32_t f = 0, c = 0;
			appendSceneCopies(vs, gi, oldCopies, asset, meshData, transforms, f, c);
			needRebuildActive = true;
			res.topologyChanged = true;
			//res.dirtyTransformRanges.push_back({ f, c });
			continue;
		}
		if (gi.usedCopies < slab.usedCopies) {
			shrinkSceneCopiesLazy(vs, sid, gi.usedCopies);
			//needRebuildActive = false; // already rebuilt inside shrink
			res.topologyChanged = true;
			continue;
		}

		// Same copy count, check transform slab relocation
		if (slab.usedCopies > 0) {
			const uint32_t expectedFirstTID = gi.firstTransform; // first copy, local = 0
			const uint32_t haveFirstTID = vs.instances[slab.first].transformID;
			if (haveFirstTID != expectedFirstTID) {
				rewriteSceneSlice(vs, gi, asset, meshData, transforms);
				res.refitOnly = true;
				// whole slice's transforms were rewritten -> mark rows dirty
				//res.dirtyTransformRanges.push_back({ slab.first, slab.usedCopies * slab.stride });
			}
		}
	}

	if (needRebuildActive) rebuildActive(vs);

	if (res.topologyChanged) res.refitOnly = false;

	return res;
}

void Visibility::appendSceneCopies(
	VisibilityState& vs,
	const GlobalInstance& gi,
	uint32_t oldCopies,
	const ModelAsset& asset,
	const std::vector<GPUMeshData>& meshData,
	const std::vector<glm::mat4>& transforms,
	uint32_t& outFirst,
	uint32_t& outCount)
{
	const uint32_t stride = gi.perInstanceStride;
	const uint32_t newCopies = gi.usedCopies;
	if (newCopies <= oldCopies) { outFirst = outCount = 0; return; }

	ASSERT(stride == asset.runtime.bakedInstances.size());

	outFirst = static_cast<uint32_t>(vs.instances.size());
	outCount = (newCopies - oldCopies) * stride;

	const size_t newSize = static_cast<size_t>(outFirst + outCount);
	vs.instances.resize(newSize);
	vs.transformIDs.resize(newSize);
	vs.worldAABBs.resize(newSize);

	uint32_t w = outFirst;
	for (uint32_t c = oldCopies; c < newCopies; ++c) {
		for (uint32_t local = 0; local < stride; ++local, ++w) {
			const GPUInstance& baked = *asset.runtime.bakedInstances[local];
			const uint32_t nodeSlot = static_cast<uint32_t>(asset.runtime.localToNodeSlot[local]);
			const uint32_t tid = transformIDFor(gi, c, nodeSlot);

			vs.instances[w] = makeRow(baked, tid, gi.drawType);
			vs.transformIDs[w] = tid;

			const uint32_t meshID = baked.meshID;
			vs.worldAABBs[w] = transformAABB(meshData[meshID].localAABB, transforms[tid]);
		}
	}

	auto& slab = vs.slabs.at(static_cast<SceneID>(gi.sceneID));
	slab.usedCopies = newCopies;
	slab.stride = stride;
}

void Visibility::shrinkSceneCopiesLazy(VisibilityState& vs, SceneID sid, uint32_t newCopies) {
	auto it = vs.slabs.find(sid);
	if (it == vs.slabs.end()) return;
	it->second.usedCopies = newCopies; // keep memory; we just rebuild 'active'
	vs.active.clear();
	for (auto& [sid2, slab] : vs.slabs) {
		for (uint32_t c = 0; c < slab.usedCopies; ++c)
			for (uint32_t local = 0; local < slab.stride; ++local)
				vs.active.push_back(slab.first + c * slab.stride + local);
	}
}

void Visibility::rewriteSceneSlice(
	VisibilityState& vs,
	const GlobalInstance& gi,
	const ModelAsset& asset,
	const std::vector<GPUMeshData>& meshData,
	const std::vector<glm::mat4>& transforms)
{
	auto it = vs.slabs.find(static_cast<SceneID>(gi.sceneID));
	if (it == vs.slabs.end()) return;
	const CoreSlab& slab = it->second;

	uint32_t w = slab.first;
	for (uint32_t c = 0; c < slab.usedCopies; ++c) {
		for (uint32_t local = 0; local < slab.stride; ++local, ++w) {
			const GPUInstance& baked = *asset.runtime.bakedInstances[local];
			const uint32_t nodeSlot = static_cast<uint32_t>(asset.runtime.localToNodeSlot[local]);
			const uint32_t tid = transformIDFor(gi, c, nodeSlot);

			vs.instances[w].transformID = tid; // keep mesh/material/pass as baked
			vs.transformIDs[w] = tid;

			const uint32_t meshID = baked.meshID;
			vs.worldAABBs[w] = transformAABB(meshData[meshID].localAABB, transforms[tid]);
		}
	}
}

void Visibility::recomputeWorldRanges(
	VisibilityState& vs,
	const std::vector<DirtyRange>& ranges,
	const std::vector<GPUMeshData>& meshData,
	const std::vector<glm::mat4>& transforms)
{
	for (const auto& r : ranges) {
		ASSERT(r.offset + r.count <= vs.instances.size());
		for (uint32_t i = 0; i < r.count; ++i) {
			const uint32_t idx = r.offset + i;
			const uint32_t meshID = vs.instances[idx].meshID;
			const uint32_t tid = vs.instances[idx].transformID;
			vs.worldAABBs[idx] = transformAABB(meshData[meshID].localAABB, transforms[tid]);
		}
	}
}

// Walk the BVH, cull and emit visible rows.
void Visibility::cullBVHCollect(
	const VisibilityState& vs,
	const Frustum& frus,
	std::vector<GPUInstance>& visibleInstances,
	std::vector<AABB>& visibleWorldAABBs)
{
	visibleInstances.clear();
	visibleWorldAABBs.clear();
	if (vs.bvh.empty()) return;

	visibleInstances.reserve(vs.active.size());
	visibleWorldAABBs.reserve(vs.active.size());

	std::vector<uint32_t> stack;
	stack.reserve(128);
	stack.push_back(0u); // root

	while (!stack.empty()) {
		const uint32_t ni = stack.back();
		stack.pop_back();
		const BVHNode& node = vs.bvh[ni];

		if (!boxInFrustum(node.box, frus)) continue;

		if (node.count) {
			const uint32_t first = node.first;
			const uint32_t last = first + node.count;
			for (uint32_t i = first; i < last; ++i) {
				const uint32_t idx = vs.leafIndex[i];
				const AABB& wb = vs.worldAABBs[idx];
				if (!boxInFrustum(wb, frus)) continue;

				visibleWorldAABBs.push_back(wb);
				visibleInstances.push_back(vs.instances[idx]);
			}
		}
		else {
			stack.push_back(static_cast<uint32_t>(node.left));
			stack.push_back(static_cast<uint32_t>(node.right));
		}
	}
}



// CPU Sided culling
// Frustum culling
bool Visibility::isVisible(const AABB aabb, const Frustum frus) {
	if (!boxInFrustum(aabb, frus)) {
		return false;
	}

	return true; // object is in the view frustum
}

bool Visibility::boxInFrustum(const AABB box, const Frustum fru) {
	const glm::vec3 center = (box.vmax + box.vmin) * 0.5f;
	const glm::vec3 extents = (box.vmax - box.vmin) * 0.5f;

	const float minSafeRadius = box.sphereRadius * 0.01f;
	const float safeRadius = glm::max(box.sphereRadius, minSafeRadius);

	//For each plane in the frustum
	for (int i = 0; i < 6; i++) {
		glm::vec3 normal = glm::vec3(fru.planes[i]);
		float d = fru.planes[i].w;

		float dist = glm::dot(normal, center) + d;

		// FIXME: need to adjust culling issues with small spheres
		if (dist < -safeRadius) return false;

		float r =
			extents.x * abs(normal.x) +
			extents.y * abs(normal.y) +
			extents.z * abs(normal.z);

		if (dist + r < 0.0f) return false;
	}

	int out;

	// check +x
	out = 0;
	for (int i = 0; i < 8; i++)
		out += (fru.points[i].x > box.vmax.x) ? 1 : 0;
	if (out == 8) return false;

	// check -x
	out = 0;
	for (int i = 0; i < 8; i++)
		out += (fru.points[i].x < box.vmin.x) ? 1 : 0;
	if (out == 8) return false;

	// check +y
	out = 0;
	for (int i = 0; i < 8; i++)
		out += (fru.points[i].y > box.vmax.y) ? 1 : 0;
	if (out == 8) return false;

	// check -y
	out = 0;
	for (int i = 0; i < 8; i++)
		out += (fru.points[i].y < box.vmin.y) ? 1 : 0;
	if (out == 8) return false;

	// check +z
	out = 0;
	for (int i = 0; i < 8; i++)
		out += (fru.points[i].z > box.vmax.z) ? 1 : 0;
	if (out == 8) return false;

	// check -z
	out = 0;
	for (int i = 0; i < 8; i++)
		out += (fru.points[i].z < box.vmin.z) ? 1 : 0;
	if (out == 8) return false;

	return true;
}

Frustum Visibility::extractFrustum(const glm::mat4 viewproj) {
	const glm::mat4 vpt = glm::transpose(viewproj);

	Frustum frustum{};
	frustum.planes[0] = vpt[3] + vpt[0]; // left
	frustum.planes[1] = vpt[3] - vpt[0]; // right
	frustum.planes[2] = vpt[3] + vpt[1]; // bot
	frustum.planes[3] = vpt[3] - vpt[1]; // top
	frustum.planes[4] = vpt[3] + vpt[2]; // near
	frustum.planes[5] = vpt[3] - vpt[2]; // far

	for (int i = 0; i < 6; ++i) {
		frustum.planes[i] /= glm::length(glm::vec3(frustum.planes[i]));
	}

	glm::mat4 invVp = glm::inverse(viewproj);
	int i = 0;
	for (int x = -1; x <= 1; x += 2) {
		for (int y = -1; y <= 1; y += 2) {
			for (int z = -1; z <= 1; z += 2) {
				glm::vec4 corner = invVp * glm::vec4(x, y, z, 1.0f);
				frustum.points[i++] = glm::vec4(corner) / corner.w;
			}
		}
	}

	return frustum;
}


AABB Visibility::transformAABB(const AABB& localBox, const glm::mat4& transform) {
	// Convert to min/max corners first
	const glm::vec3 vmin = localBox.vmin;
	const glm::vec3 vmax = localBox.vmax;

	const glm::vec3 corners[8] = {
		glm::vec3(transform * glm::vec4(vmin.x, vmin.y, vmin.z, 1.0f)),
		glm::vec3(transform * glm::vec4(vmin.x, vmax.y, vmin.z, 1.0f)),
		glm::vec3(transform * glm::vec4(vmin.x, vmin.y, vmax.z, 1.0f)),
		glm::vec3(transform * glm::vec4(vmin.x, vmax.y, vmax.z, 1.0f)),
		glm::vec3(transform * glm::vec4(vmax.x, vmin.y, vmin.z, 1.0f)),
		glm::vec3(transform * glm::vec4(vmax.x, vmax.y, vmin.z, 1.0f)),
		glm::vec3(transform * glm::vec4(vmax.x, vmin.y, vmax.z, 1.0f)),
		glm::vec3(transform * glm::vec4(vmax.x, vmax.y, vmax.z, 1.0f))
	};

	// Now apply the min/max algorithm from before using the 8 transformed corners
	glm::vec3 newVmin = corners[0];
	glm::vec3 newVmax = newVmin;

	// Start looping from corner 1 onwards
	for (size_t i = 1; i < 8; ++i) {
		const auto& current = corners[i];
		newVmin = glm::min(newVmin, current);
		newVmax = glm::max(newVmax, current);
	}

	AABB worldBox{};
	worldBox.vmin = newVmin;
	worldBox.vmax = newVmax;
	worldBox.origin = (newVmax + newVmin) * 0.5f;
	worldBox.extent = (newVmax - newVmin) * 0.5f;
	worldBox.sphereRadius = glm::length(worldBox.extent);

	return worldBox;
}

std::vector<glm::vec3> Visibility::GetAABBVertices(const AABB box) {
	const glm::vec3 vmin = box.vmin;
	const glm::vec3 vmax = box.vmax;

	const glm::vec3 corners[8] {
		glm::vec3(vmin.x, vmin.y, vmin.z),
		glm::vec3(vmin.x, vmax.y, vmin.z),
		glm::vec3(vmin.x, vmin.y, vmax.z),
		glm::vec3(vmin.x, vmax.y, vmax.z),
		glm::vec3(vmax.x, vmin.y, vmin.z),
		glm::vec3(vmax.x, vmax.y, vmin.z),
		glm::vec3(vmax.x, vmin.y, vmax.z),
		glm::vec3(vmax.x, vmax.y, vmax.z)
	};

	// Now connect the corners to form 12 lines
	std::vector<glm::vec3> vertices = {
		corners[0], corners[1],
		corners[2], corners[3],
		corners[4], corners[5],
		corners[6], corners[7],

		corners[0], corners[2],
		corners[1], corners[3],
		corners[4], corners[6],
		corners[5], corners[7],

		corners[0], corners[4],
		corners[1], corners[5],
		corners[2], corners[6],
		corners[3], corners[7]
	};

	return vertices;
}