#include "pch.h"

#include "AssetManager.h"
#include "AssetRegistry.h"
#include "SceneCache.h"
#include "Mesh.h"
#include "Vertex.h"
#include "Material.h"
#include "../JobSystem.h"
#include "importers/BCNCompression.h"

static void HeightToNormalRGBA(std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, float strength);
static void GlossToRoughRGBA(std::vector<uint8_t>& rgba, uint32_t w, uint32_t h);
static bool CompositeAlphaMask(
	std::vector<uint8_t>& rgba, uint32_t w, uint32_t h,
	const std::filesystem::path& maskPath);

namespace
{
	constexpr float MIN_LIGHT_LUMINANCE = 0.01f;

	SceneLightDesc MakeLightDesc(
		const SourceLight& src, const glm::mat4& world, float intensityScale)
	{
		SceneLightDesc d{};
		d.position = glm::vec3(world[3]);
		d.color = src.color;
		d.intensity = src.intensity * intensityScale;
		d.type = src.type;
		d.innerCos = std::cos(src.innerConeAngle);
		d.outerCos = std::cos(src.outerConeAngle);

		const glm::vec3 fwd = glm::vec3(world[2]);
		d.direction = (glm::dot(fwd, fwd) > 1e-12f)
			? -glm::normalize(fwd)
			: glm::vec3(0, 0, -1);

		d.range = (src.range > 0.0f)
			? src.range
			: std::sqrt(std::max(d.intensity, 1e-4f) / MIN_LIGHT_LUMINANCE);

		return d;
	}
}

// -----------------------------------------------------------------------
// LoadScenes
// -----------------------------------------------------------------------

void AssetManager::LoadScenes(SceneBatchReadyCallback onBatchReady, JobSystem& jobSystem)
{
	const auto& registry = GetAssetRegistry();
	if (registry.empty()) return;

	m_queues = std::make_shared<StageQueues>();
	m_queues->onBatchReady = std::move(onBatchReady);
	m_queues->jobSystem = &jobSystem;

	for (const auto& [sceneID, entry] : registry)
	{
		auto context = std::make_shared<SceneJobContext>();
		context->batch = std::make_shared<SceneUploadBatch>();
		context->filePath = std::filesystem::path(BaseAssetPath) / entry.relativePath;
		context->importOptions = entry.options;

		context->batch->sceneID = sceneID;
		context->batch->sceneName = entry.relativePath;

		m_queues->loadFile.Push(context);
		m_queues->pendingSceneCount++;
	}

	auto queues = m_queues;

	const uint32_t workers = std::max(1u, jobSystem.GetWorkerCount());

	for (uint32_t i = 0; i < workers; ++i)
	{
		jobSystem.SubmitJob([this, queues](ThreadContext& ctx)
			{
				ScopedWorkQueue scoped(ctx, queues.get());

				while (!queues->IsFullyDrained())
				{
					bool didWork = false;
					didWork |= StageLoadFile(ctx);
					didWork |= StageDecodeImages(ctx);
					didWork |= StageBuildSamplers(ctx);
					didWork |= StageProcessMaterials(ctx);
					didWork |= StageProcessMeshes(ctx);
					didWork |= StageBuildSceneGraph(ctx);

					if (!didWork)
						std::this_thread::yield();
				}
			});
	}
}

// -----------------------------------------------------------------------
// StageLoadFile
// -----------------------------------------------------------------------

bool AssetManager::StageLoadFile(ThreadContext& ctx)
{
	auto* sq = static_cast<StageQueues*>(ctx.workQueueActive);
	auto jobs = sq->loadFile.Collect();
	if (jobs.empty()) return false;

	for (auto& context : jobs)
	{
		fmt::println("[AssetManager] Loading: {}", context->filePath.string());

		if (SceneCache::Load(context->filePath, context->importOptions,
			*context->batch))
		{
			context->fromCache = true;
			sq->Advance(context, AssetJobType::LoadFile);
			++ctx.jobsExecuted;
			continue;
		}

		if (!ImportScene(context->filePath, context->importOptions, context->source))
		{
			sq->pendingSceneCount--;
			continue;
		}

		sq->Advance(context, AssetJobType::LoadFile);
		++ctx.jobsExecuted;
	}
	return true;
}

// -----------------------------------------------------------------------
// StageDecodeImages
// -----------------------------------------------------------------------

static void DecodeOneImage(const SourceImage& img, TextureDesc& desc)
{
	desc.debugName = img.name;
	desc.isSRGB = img.isSRGB;

	int w = 0, h = 0, ch = 0;
	uint8_t* raw = nullptr;

	if (!img.encodedBytes.empty())
		raw = stbi_load_from_memory(img.encodedBytes.data(),
			static_cast<int>(img.encodedBytes.size()), &w, &h, &ch, 4);
	else if (!img.filePath.empty())
		raw = stbi_load(img.filePath.string().c_str(), &w, &h, &ch, 4);

	if (raw && w > 0 && h > 0)
	{
		std::vector<uint8_t> level(raw, raw + (static_cast<size_t>(w) * h * 4u));
		stbi_image_free(raw);

		if (img.isHeightMap) HeightToNormalRGBA(level, w, h, img.bumpStrength);
		if (img.isGlossMap)  GlossToRoughRGBA(level, w, h);
		if (!img.alphaMaskPath.empty()) CompositeAlphaMask(level, w, h, img.alphaMaskPath);

		desc.width = static_cast<uint32_t>(w);
		desc.height = static_cast<uint32_t>(h);

		desc.format = img.isNormalMap ? TextureFormat::BC5 : TextureFormat::BC7;

		if (w < 4 || h < 4) desc.format = TextureFormat::RGBA8;

		const bool compress = (w >= 4 && h >= 4);
		if (!compress) desc.format = TextureFormat::RGBA8;

		uint32_t mw = desc.width, mh = desc.height;
		std::vector<uint8_t> scratch;

		while (true)
		{
			std::vector<uint8_t> encoded;
			switch (desc.format)
			{
			case TextureFormat::BC7: CompressBC7(level, mw, mh, encoded); break;
			case TextureFormat::BC5: CompressBC5(level, mw, mh, encoded); break;
			default:                 encoded = level;                     break;
			}

			TextureMipDesc mip{};
			mip.width = mw;
			mip.height = mh;
			mip.offset = static_cast<uint32_t>(desc.pixelData.size());
			mip.bytes = static_cast<uint32_t>(encoded.size());
			desc.mips.push_back(mip);

			desc.pixelData.insert(desc.pixelData.end(), encoded.begin(), encoded.end());

			if (mw <= 4 && mh <= 4) break;

			const uint32_t nw = std::max(1u, mw >> 1);
			const uint32_t nh = std::max(1u, mh >> 1);
			DownsampleBox(level, mw, mh, img.isSRGB, scratch, nw, nh);
			level.swap(scratch);
			mw = nw; mh = nh;
		}
	}
	else
	{
		fmt::println("[AssetManager] Decode failed: {}", desc.debugName);
	}
}

bool AssetManager::StageDecodeImages(ThreadContext& ctx)
{
	auto* sq = static_cast<StageQueues*>(ctx.workQueueActive);
	auto jobs = sq->decodeImages.Collect();
	if (jobs.empty()) return false;

	for (auto& context : jobs)
	{
		if (!context->CanRun(AssetJobType::DecodeImages))
		{
			sq->decodeImages.Push(context);
			continue;
		}

		auto& batch = *context->batch;
		auto& images = context->source.images;

		batch.textures.assign(images.size(), TextureDesc{});

		sq->jobSystem->RunParallel(static_cast<uint32_t>(images.size()),
			[&](ThreadContext&, uint32_t i)
			{
				DecodeOneImage(images[i], batch.textures[i]);
			});

		sq->Advance(context, AssetJobType::DecodeImages);

		++ctx.jobsExecuted;
	}

	return true;
}

// -----------------------------------------------------------------------
// StageBuildSamplers
// -----------------------------------------------------------------------

bool AssetManager::StageBuildSamplers(ThreadContext& ctx)
{
	auto* sq = static_cast<StageQueues*>(ctx.workQueueActive);
	auto jobs = sq->buildSamplers.Collect();
	if (jobs.empty()) return false;

	for (auto& context : jobs)
	{
		if (!context->CanRun(AssetJobType::BuildSamplers))
		{
			sq->buildSamplers.Push(context);
			continue;
		}

		auto& batch = *context->batch;
		batch.samplers = context->source.samplers;

		if (batch.samplers.empty())
			batch.samplers.push_back(SamplerDesc{});

		sq->Advance(context, AssetJobType::BuildSamplers);
		++ctx.jobsExecuted;
	}
	return true;
}

// -----------------------------------------------------------------------
// StageProcessMaterials
// -----------------------------------------------------------------------

bool AssetManager::StageProcessMaterials(ThreadContext& ctx)
{
	auto* sq = static_cast<StageQueues*>(ctx.workQueueActive);
	auto jobs = sq->processMaterials.Collect();
	if (jobs.empty()) return false;

	for (auto& context : jobs)
	{
		if (!context->CanRun(AssetJobType::ProcessMaterials))
		{
			sq->processMaterials.Push(context);
			continue;
		}

		auto& batch = *context->batch;
		batch.materials = context->source.materials;

		batch.materialFlags.clear();
		batch.materialFlags.reserve(batch.materials.size());
		for (const auto& desc : batch.materials)
			batch.materialFlags.push_back(desc.flags);

		sq->Advance(context, AssetJobType::ProcessMaterials);
		++ctx.jobsExecuted;
	}
	return true;
}

// -----------------------------------------------------------------------
// StageProcessMeshes
// -----------------------------------------------------------------------

struct MeshletBuild
{
	std::vector<Meshlet>  meshlets;
	std::vector<uint32_t> verts;
	std::vector<uint8_t>  tris;
};

static void BuildMeshletRangeLocal(
	const Vertex* verts,
	uint32_t        vertexCount,
	const uint32_t* indices,
	size_t          indexCount,
	MeshletBuild& out)
{
	out.meshlets.clear(); out.verts.clear(); out.tris.clear();
	if (indexCount < 3 || (indexCount % 3) != 0) return;

	const float* posPtr = reinterpret_cast<const float*>(
		reinterpret_cast<const uint8_t*>(verts) + offsetof(Vertex, position));

	const size_t bound = meshopt_buildMeshletsBound(
		indexCount, MESHLET_MAX_VERTS, MESHLET_MAX_TRIS);

	std::vector<meshopt_Meshlet> raw(bound);
	out.verts.resize(bound * MESHLET_MAX_VERTS);
	out.tris.resize(bound * MESHLET_MAX_TRIS * 3);

	const size_t count = meshopt_buildMeshlets(
		raw.data(), out.verts.data(), out.tris.data(),
		indices, indexCount,
		posPtr, vertexCount, sizeof(Vertex),
		MESHLET_MAX_VERTS, MESHLET_MAX_TRIS, MESHLET_CONE_WEIGHT);

	if (count == 0) { out.verts.clear(); out.tris.clear(); return; }

	out.meshlets.reserve(count);
	for (size_t i = 0; i < count; ++i)
	{
		meshopt_Meshlet& m = raw[i];

		meshopt_optimizeMeshlet(
			&out.verts[m.vertex_offset], &out.tris[m.triangle_offset],
			m.triangle_count, m.vertex_count);

		const meshopt_Bounds b = meshopt_computeMeshletBounds(
			&out.verts[m.vertex_offset], &out.tris[m.triangle_offset],
			m.triangle_count, posPtr, vertexCount, sizeof(Vertex));

		Meshlet ml{};
		ml.center = { b.center[0], b.center[1], b.center[2] };
		ml.radius = b.radius;
		ml.coneAxis[0] = b.cone_axis_s8[0];
		ml.coneAxis[1] = b.cone_axis_s8[1];
		ml.coneAxis[2] = b.cone_axis_s8[2];
		ml.coneCutoff = b.cone_cutoff_s8;
		ml.vertexOffset = m.vertex_offset;      // local, rebased on merge
		ml.triangleOffset = m.triangle_offset;
		ml.vertexCount = static_cast<uint8_t>(m.vertex_count);
		ml.triangleCount = static_cast<uint8_t>(m.triangle_count);

		out.meshlets.push_back(ml);
	}

	const meshopt_Meshlet& last = raw[count - 1];
	out.verts.resize(last.vertex_offset + last.vertex_count);
	out.tris.resize(last.triangle_offset + ((last.triangle_count * 3u + 3u) & ~3u));
}

bool AssetManager::StageProcessMeshes(ThreadContext& ctx)
{
	auto* sq = static_cast<StageQueues*>(ctx.workQueueActive);
	auto jobs = sq->processMeshes.Collect();
	if (jobs.empty()) return false;

	auto& rawVerts = ctx.scratch.bufferA;
	auto& rawIdx = ctx.scratch.bufferB;
	auto& rawLodIdx = ctx.scratch.bufferC;

	for (auto& context : jobs)
	{
		if (!context->CanRun(AssetJobType::ProcessMeshes))
		{
			sq->processMeshes.Push(context);
			continue;
		}

		auto& src = context->source;
		auto& batch = *context->batch;

		size_t totalVerts = 0, totalIndices = 0;
		for (const auto& node : src.nodes)
			for (const auto& prim : node.primitives)
			{
				totalVerts += prim.vertices.size();
				totalIndices += prim.indices.size();
			}

		batch.vertices.resize(totalVerts);
		batch.indices.resize(totalIndices);

		size_t vtxCursor = 0, idxCursor = 0;

		for (uint32_t nodeIdx = 0; nodeIdx < static_cast<uint32_t>(src.nodes.size()); ++nodeIdx)
		{
			auto& srcNode = src.nodes[nodeIdx];

			for (auto& prim : srcNode.primitives)
			{
				const uint32_t vCnt = static_cast<uint32_t>(prim.vertices.size());
				const uint32_t iCnt = static_cast<uint32_t>(prim.indices.size());
				if (vCnt == 0 || iCnt < 3) continue;

				std::memcpy(batch.vertices.data() + vtxCursor,
					prim.vertices.data(), vCnt * sizeof(Vertex));
				std::memcpy(batch.indices.data() + idxCursor,
					prim.indices.data(), iCnt * sizeof(uint32_t));

				uint32_t* idxPtr = batch.indices.data() + idxCursor;
				Vertex* vtxPtr = batch.vertices.data() + vtxCursor;

				const uint32_t localMaterialIdx = prim.materialIdx;
				const uint32_t passType = src.materials[localMaterialIdx].passType;
				const uint32_t matFlags = src.materials[localMaterialIdx].flags;

				const float* posPtr = reinterpret_cast<const float*>(
					reinterpret_cast<const uint8_t*>(vtxPtr) + offsetof(Vertex, position));

				glm::vec3 vmin = vtxPtr[0].position, vmax = vmin;
				for (uint32_t i = 0; i < vCnt; ++i)
				{
					vmin = glm::min(vmin, vtxPtr[i].position);
					vmax = glm::max(vmax, vtxPtr[i].position);
				}
				const float simplifyScale = meshopt_simplifyScale(posPtr, vCnt, sizeof(Vertex));

				meshopt_optimizeVertexCache(idxPtr, idxPtr, iCnt, vCnt);
				if ((iCnt % 3u) == 0u)
					meshopt_optimizeOverdraw(idxPtr, idxPtr, iCnt, posPtr, vCnt, sizeof(Vertex), 1.05f);

				rawVerts.resize(vCnt * sizeof(Vertex));
				meshopt_optimizeVertexFetch(
					reinterpret_cast<Vertex*>(rawVerts.data()),
					idxPtr, iCnt, vtxPtr, vCnt, sizeof(Vertex));
				std::memcpy(vtxPtr, rawVerts.data(), vCnt * sizeof(Vertex));

				MeshDesc meshDesc{};
				meshDesc.firstIndex = static_cast<uint32_t>(idxCursor);
				meshDesc.indexCount = iCnt;
				meshDesc.vertexOffset = static_cast<uint32_t>(vtxCursor);
				meshDesc.vertexCount = vCnt;
				meshDesc.localAABB.vmin = vmin;
				meshDesc.localAABB.vmax = vmax;
				meshDesc.localBoundingRadius = MeshRegistry::ComputeLocalBoundingRadius(vtxPtr, vCnt, vmin, vmax);

				if (matFlags & MATERIAL_FLAG_ALPHA_MASKED)
					meshDesc.flags |= MESH_FLAG_ALPHA_TESTED;

				const uint32_t lod0Idx = static_cast<uint32_t>(batch.meshes.size());
				meshDesc.lod0 = lod0Idx;
				batch.meshes.push_back(meshDesc);

				rawIdx.resize(iCnt * sizeof(uint32_t));
				std::memcpy(rawIdx.data(), idxPtr, iCnt * sizeof(uint32_t));

				auto buildLOD = [&](float ratio, float error) -> uint32_t
					{
						size_t target = std::max<size_t>(3u, static_cast<size_t>(iCnt * ratio) / 3 * 3);
						if (target >= iCnt) return UINT32_MAX;
						rawLodIdx.resize(iCnt * sizeof(uint32_t));
						size_t lodCount = meshopt_simplify(
							reinterpret_cast<uint32_t*>(rawLodIdx.data()),
							reinterpret_cast<uint32_t*>(rawIdx.data()),
							iCnt, posPtr, vCnt, sizeof(Vertex),
							target, error * simplifyScale);
						if (lodCount < 3 || (lodCount % 3) != 0) return UINT32_MAX;
						rawLodIdx.resize(lodCount * sizeof(uint32_t));

						auto* lodIdxPtr = reinterpret_cast<uint32_t*>(rawLodIdx.data());
						meshopt_optimizeVertexCache(lodIdxPtr, lodIdxPtr, lodCount, vCnt);
						if ((lodCount % 3u) == 0u)
							meshopt_optimizeOverdraw(lodIdxPtr, lodIdxPtr, lodCount, posPtr, vCnt, sizeof(Vertex), 1.05f);

						MeshDesc lodMesh = meshDesc;
						lodMesh.firstIndex = static_cast<uint32_t>(batch.indices.size());
						lodMesh.indexCount = static_cast<uint32_t>(lodCount);
						lodMesh.flags |= MESH_FLAG_IS_LOD_VARIANT;
						const uint32_t lodIdx = static_cast<uint32_t>(batch.meshes.size());
						batch.indices.insert(batch.indices.end(), lodIdxPtr, lodIdxPtr + lodCount);
						batch.meshes.push_back(lodMesh);
						return lodIdx;
					};

				if ((iCnt % 3u) == 0u)
				{
					const uint32_t lod1 = buildLOD(0.60f, 0.005f);
					const uint32_t lod2 = buildLOD(0.35f, 0.010f);
					const uint32_t lod3 = buildLOD(0.20f, 0.020f);

					const bool thinMesh = MeshRegistry::IsThinMeshForShadows(
						Mesh{ .localAABB = { vmin, vmax } });

					const bool isFoliage = (matFlags & MATERIAL_FLAG_ALPHA_MASKED)
						|| (matFlags & MATERIAL_FLAG_IS_TREE);

					const bool isDoubleSided = (matFlags & MATERIAL_FLAG_DOUBLE_SIDED);

					const bool forceLod0 = thinMesh || iCnt < 300u || isFoliage || isDoubleSided;

					const glm::vec3 extent = vmax - vmin;
					const bool isGoodOccludee =
						!thinMesh && !isFoliage && iCnt >= 300u &&
						(extent.x * extent.y * extent.z) > 0.001f;

					uint32_t shadowLod0 = lod0Idx;
					uint32_t shadowLod1 = lod0Idx;
					uint32_t shadowLod2 = lod0Idx;

					if (!forceLod0)
					{
						shadowLod1 = buildLOD(0.75f, 0.002f);
						shadowLod2 = buildLOD(0.55f, 0.004f);
						if (shadowLod1 == UINT32_MAX) shadowLod1 = lod0Idx;
						if (shadowLod2 == UINT32_MAX) shadowLod2 = shadowLod1;
					}

					auto& md = batch.meshes[lod0Idx];
					md.flags |= MESH_FLAG_IS_LOD;

					md.lod1 = (lod1 != UINT32_MAX) ? lod1 : lod0Idx;
					md.lod2 = (lod2 != UINT32_MAX) ? lod2 : md.lod1;
					md.lod3 = (lod3 != UINT32_MAX) ? lod3 : md.lod2;

					md.shadowLod0 = shadowLod0;
					md.shadowLod1 = shadowLod1;
					md.shadowLod2 = shadowLod2;

					if (forceLod0)      md.flags |= MESH_LOD_FLAG_FORCE_SHADOW_LOD0;
					if (isGoodOccludee) md.flags |= MESH_FLAG_GOOD_OCCLUDEE;
				}

				for (uint32_t transformIdx : srcNode.transformIndices)
					batch.instances.emplace_back(InstanceDesc{
						.localMeshIdx = lod0Idx,
						.localMaterialIdx = localMaterialIdx,
						.nodeIdx = transformIdx,
						.passType = passType });

				vtxCursor += vCnt;
				idxCursor += iCnt;
			}

			srcNode.primitives.clear();
			srcNode.primitives.shrink_to_fit();
		}

		// Shadow index buffer
		const size_t shadowBase = batch.indices.size();

		size_t shadowTotal = 0;
		for (const auto& md : batch.meshes)
			if ((md.flags & MESH_FLAG_IS_LOD_VARIANT) == 0u)
				shadowTotal += md.indexCount;
		batch.indices.resize(shadowBase + shadowTotal);

		std::vector<uint32_t> shadowJobs;
		shadowJobs.reserve(batch.meshes.size());

		size_t shadowCursor = shadowBase;
		for (uint32_t i = 0; i < static_cast<uint32_t>(batch.meshes.size()); ++i)
		{
			auto& md = batch.meshes[i];

			if (md.flags & MESH_FLAG_IS_LOD_VARIANT)
			{
				md.shadowFirstIndex = md.firstIndex;
				md.shadowIndexCount = md.indexCount;
				continue;
			}

			md.shadowFirstIndex = static_cast<uint32_t>(shadowCursor);
			md.shadowIndexCount = md.indexCount;
			shadowJobs.push_back(i);
			shadowCursor += md.indexCount;
		}
		ASSERT(shadowCursor == shadowBase + shadowTotal);

		sq->jobSystem->RunParallel(static_cast<uint32_t>(shadowJobs.size()),
			[&](ThreadContext&, uint32_t j)
			{
				const auto& md = batch.meshes[shadowJobs[j]];
				const Vertex* verts = batch.vertices.data() + md.vertexOffset;

				meshopt_Stream streams[2]{};
				streams[0].data = reinterpret_cast<const uint8_t*>(verts) + offsetof(Vertex, position);
				streams[0].size = sizeof(float) * 3u;
				streams[0].stride = sizeof(Vertex);

				size_t streamCount = 1;

				if (md.flags & MESH_FLAG_ALPHA_TESTED)
				{
					streams[1].data = reinterpret_cast<const uint8_t*>(verts) + offsetof(Vertex, uvX);
					streams[1].size = sizeof(uint16_t) * 2u;
					streams[1].stride = sizeof(Vertex);
					streamCount = 2;
				}

				meshopt_generateShadowIndexBufferMulti(
					batch.indices.data() + md.shadowFirstIndex,
					batch.indices.data() + md.firstIndex,
					md.shadowIndexCount, md.vertexCount, streams, streamCount);
			});

		// Meshlets
		const uint32_t meshCount = static_cast<uint32_t>(batch.meshes.size());
		std::vector<MeshletBuild> renderBuilds(meshCount);
		std::vector<MeshletBuild> shadowBuilds(meshCount);
		std::vector<uint8_t>      sharesShadow(meshCount, 0);

		sq->jobSystem->RunParallel(meshCount, [&](ThreadContext&, uint32_t i)
			{
				const auto& md = batch.meshes[i];
				const Vertex* verts = batch.vertices.data() + md.vertexOffset;

				BuildMeshletRangeLocal(verts, md.vertexCount,
					batch.indices.data() + md.firstIndex, md.indexCount,
					renderBuilds[i]);

				if (md.shadowFirstIndex == md.firstIndex)
				{
					sharesShadow[i] = 1;
					return;
				}

				BuildMeshletRangeLocal(verts, md.vertexCount,
					batch.indices.data() + md.shadowFirstIndex, md.shadowIndexCount,
					shadowBuilds[i]);
			});

		auto appendBuild = [&](MeshletBuild& b, uint32_t& outOffset, uint32_t& outCount)
			{
				outOffset = static_cast<uint32_t>(batch.meshlets.size());
				outCount = static_cast<uint32_t>(b.meshlets.size());
				if (outCount == 0) return;

				const uint32_t vBase = static_cast<uint32_t>(batch.meshletVertices.size());
				const uint32_t tBase = static_cast<uint32_t>(batch.meshletTriangles.size());

				for (auto& ml : b.meshlets)
				{
					ml.vertexOffset += vBase;
					ml.triangleOffset += tBase;
					batch.meshlets.push_back(ml);
				}

				batch.meshletVertices.insert(batch.meshletVertices.end(), b.verts.begin(), b.verts.end());
				batch.meshletTriangles.insert(batch.meshletTriangles.end(), b.tris.begin(), b.tris.end());

				b = MeshletBuild{};
			};

		for (uint32_t i = 0; i < meshCount; ++i)
		{
			auto& md = batch.meshes[i];

			appendBuild(renderBuilds[i], md.meshletOffset, md.meshletCount);

			if (sharesShadow[i])
			{
				md.shadowMeshletOffset = md.meshletOffset;
				md.shadowMeshletCount = md.meshletCount;
			}
			else
			{
				appendBuild(shadowBuilds[i], md.shadowMeshletOffset, md.shadowMeshletCount);
			}
		}

		// Per-LOD visibility bit ranges
		for (auto& md : batch.meshes)
		{
			if ((md.flags & MESH_FLAG_IS_LOD) == 0u) continue;

			const uint32_t chain[4] = { md.lod0, md.lod1, md.lod2, md.lod3 };
			uint32_t cursor = 0u;

			for (uint32_t i = 0; i < 4u; ++i)
			{
				bool dup = false;
				for (uint32_t j = 0; j < i; ++j)
					if (chain[j] == chain[i]) { dup = true; break; }
				if (dup) continue;

				MeshDesc& lodMd = batch.meshes[chain[i]];
				lodMd.meshletVisibilityBase = cursor;
				cursor += lodMd.meshletCount;
			}
		}

		sq->Advance(context, AssetJobType::ProcessMeshes);
		++ctx.jobsExecuted;
	}

	return true;
}

// -----------------------------------------------------------------------
// StageBuildSceneGraph
// -----------------------------------------------------------------------

bool AssetManager::StageBuildSceneGraph(ThreadContext& ctx)
{
	auto* sq = static_cast<StageQueues*>(ctx.workQueueActive);
	auto jobs = sq->buildSceneGraph.Collect();
	if (jobs.empty()) return false;

	for (auto& context : jobs)
	{
		if (!context->CanRun(AssetJobType::BuildSceneGraph))
		{
			sq->buildSceneGraph.Push(context);
			continue;
		}

		auto& src = context->source;
		auto& batch = *context->batch;

		std::unordered_map<uint32_t, uint32_t> nodeToSlot;
		std::vector<uint32_t> uniqueNodeIDs;

		for (const auto& inst : batch.instances)
			if (nodeToSlot.find(inst.nodeIdx) == nodeToSlot.end())
			{
				nodeToSlot[inst.nodeIdx] = static_cast<uint32_t>(uniqueNodeIDs.size());
				uniqueNodeIDs.push_back(inst.nodeIdx);
			}

		batch.localToNodeSlot.resize(batch.instances.size());
		for (size_t i = 0; i < batch.instances.size(); ++i)
			batch.localToNodeSlot[i] = nodeToSlot[batch.instances[i].nodeIdx];

		batch.nodeTransforms.reserve(uniqueNodeIDs.size());
		for (uint32_t idx : uniqueNodeIDs)
			batch.nodeTransforms.push_back(src.transforms[idx]);

		batch.lights.reserve(src.lights.size());
		for (const auto& light : src.lights)
		{
			if (light.transformIndex >= src.transforms.size()) continue;

			batch.lights.push_back(MakeLightDesc(
				light,
				src.transforms[light.transformIndex],
				context->importOptions.lightIntensityScale));
		}

		VirtualInstance vi{};
		vi.sceneID = static_cast<uint8_t>(batch.sceneID);
		vi.perInstanceStride = static_cast<uint32_t>(batch.instances.size());
		vi.transformCount = static_cast<uint32_t>(uniqueNodeIDs.size());
		vi.baseTransform = batch.nodeTransforms.empty()
			? glm::mat4(1.0f) : batch.nodeTransforms[0];

		batch.virtualInstance = vi;

		if (context->importOptions.useCache)
			SceneCache::Store(context->filePath, context->importOptions, batch);

		for (auto& img : src.images)
		{
			img.encodedBytes.clear();
			img.encodedBytes.shrink_to_fit();
		}

		sq->Advance(context, AssetJobType::BuildSceneGraph);
		++ctx.jobsExecuted;
	}
	return true;
}

// -----------------------------------------------------------------------
// Shutdown
// -----------------------------------------------------------------------

void AssetManager::Shutdown(JobSystem& jobSystem)
{
	jobSystem.Wait();
	m_queues.reset();
}

static void HeightToNormalRGBA(std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, float strength)
{
	if (w < 3 || h < 3) return;

	std::vector<float> height(static_cast<size_t>(w) * h);
	for (size_t i = 0; i < height.size(); ++i)
		height[i] = rgba[i * 4u] * (1.0f / 255.0f);

	const int iw = static_cast<int>(w);
	const int ih = static_cast<int>(h);

	auto at = [&](int x, int y) -> float
		{
			x = glm::clamp(x, 0, iw - 1);
			y = glm::clamp(y, 0, ih - 1);
			return height[static_cast<size_t>(y) * w + x];
		};

	for (int y = 0; y < ih; ++y)
		for (int x = 0; x < iw; ++x)
		{
			const float tl = at(x - 1, y - 1), t = at(x, y - 1), tr = at(x + 1, y - 1);
			const float l = at(x - 1, y), r = at(x + 1, y);
			const float bl = at(x - 1, y + 1), b = at(x, y + 1), br = at(x + 1, y + 1);

			const float dx = (tr + 2.0f * r + br) - (tl + 2.0f * l + bl);
			const float dy = (bl + 2.0f * b + br) - (tl + 2.0f * t + tr);

			const glm::vec3 n = glm::normalize(glm::vec3(-dx * strength, -dy * strength, 1.0f));

			const size_t o = (static_cast<size_t>(y) * w + x) * 4u;
			rgba[o + 0] = static_cast<uint8_t>((n.x * 0.5f + 0.5f) * 255.0f + 0.5f);
			rgba[o + 1] = static_cast<uint8_t>((n.y * 0.5f + 0.5f) * 255.0f + 0.5f);
			rgba[o + 2] = static_cast<uint8_t>((n.z * 0.5f + 0.5f) * 255.0f + 0.5f);
			rgba[o + 3] = 255u;
		}
}

static void GlossToRoughRGBA(std::vector<uint8_t>& rgba, uint32_t w, uint32_t h)
{
	const size_t texels = static_cast<size_t>(w) * h;

	for (size_t i = 0; i < texels; ++i)
	{
		const uint8_t gloss = rgba[i * 4u];

		rgba[i * 4u + 0] = 0u;
		rgba[i * 4u + 1] = static_cast<uint8_t>(255u - gloss);
		rgba[i * 4u + 2] = 0u;
		rgba[i * 4u + 3] = 255u;
	}
}

static bool CompositeAlphaMask(
	std::vector<uint8_t>& rgba, uint32_t w, uint32_t h,
	const std::filesystem::path& maskPath)
{
	int mw = 0, mh = 0, mch = 0;
	uint8_t* mask = stbi_load(maskPath.string().c_str(), &mw, &mh, &mch, 1);
	if (!mask) return false;

	bool ok = false;

	if (static_cast<uint32_t>(mw) == w && static_cast<uint32_t>(mh) == h)
	{
		const size_t texels = static_cast<size_t>(w) * h;
		for (size_t i = 0; i < texels; ++i)
			rgba[i * 4u + 3u] = mask[i];
		ok = true;
	}
	else
	{
		fmt::println("[AssetManager] Alpha mask size mismatch: {} ({}x{} vs {}x{})",
			maskPath.filename().string(), mw, mh, w, h);
	}

	stbi_image_free(mask);
	return ok;
}
