#include "pch.h"

#include "AssetManager.h"
#include "Mesh.h"
#include "Vertex.h"
#include "Material.h"
#include "JobSystem.h"

static const std::unordered_map<ModelID, std::string> AssetPaths
{
	{ ModelID::Sponza,            "sponza.glb"               },
	//{ ModelID::Bistro,            "Bistro.glb"               },
	//{ ModelID::MRSpheres,         "MetalRoughSpheres.glb"    },
	//{ ModelID::Duck,              "Duck.glb"                 },
	//{ ModelID::DamagedHelmet,     "DamagedHelmet.glb"        },
	//{ ModelID::DragonAttenuation, "DragonAttenuation.glb"    },
	//{ ModelID::City,              "city/town4new.glb"        },
	//{ ModelID::Structure,         "structure.glb"            },
	//{ ModelID::EmissiveTest,      "EmissiveStrengthTest.glb" },
	//{ ModelID::WrathDragon,       "wrath_of_the_dragon.glb"  },
	//{ ModelID::Mech,              "mech.glb"                 },
	//{ ModelID::YellowMech,        "yellow_mech.glb"          },
	//{ ModelID::Mini,              "mini.glb"                 }
};

static bool IsSRGBTexture(const std::string& name)
{
	return name.find("_BaseColor") != std::string::npos
		|| name.find("_Albedo")    != std::string::npos
		|| name.find("diffuse")    != std::string::npos
		|| name.find("_Emissive")  != std::string::npos
		|| name.find("emissive")   != std::string::npos;
}

static bool IsTreeMaterial(const fastgltf::Material& mat)
{
	if (mat.name.empty()) return false;
	std::string lower(mat.name.begin(), mat.name.end());
	std::transform(lower.begin(), lower.end(), lower.begin(),
		[](unsigned char c) { return std::tolower(c); });
	return lower.find("tree") != std::string::npos;
}

static SamplerDesc ExtractSamplerDesc(const fastgltf::Sampler& sampler)
{
	SamplerDesc desc{};

	auto minFilter = sampler.minFilter.value_or(fastgltf::Filter::LinearMipMapLinear);

	desc.isLinear = true;
	switch (sampler.magFilter.value_or(fastgltf::Filter::Linear))
	{
	case fastgltf::Filter::Nearest:
	case fastgltf::Filter::NearestMipMapNearest:
	case fastgltf::Filter::NearestMipMapLinear:
		desc.isLinear = false;
		break;
	default:
		break;
	}

	switch (minFilter)
	{
	case fastgltf::Filter::NearestMipMapNearest:
	case fastgltf::Filter::LinearMipMapNearest:
	case fastgltf::Filter::NearestMipMapLinear:
	case fastgltf::Filter::LinearMipMapLinear:
		desc.isMipMapped = true;
		break;
	default:
		desc.isMipMapped = false;
		break;
	}

	desc.anisotropy = desc.isMipMapped ? RD::ANISOTROPY_LEVEL_16 : 1.0f;
	return desc;
}

// -----------------------------------------------------------------------
// AssetManager::LoadScenes
// -----------------------------------------------------------------------

void AssetManager::LoadScenes(
	SceneBatchReadyCallback      onBatchReady,
	JobSystem&                   jobSystem)
{
	if (AssetPaths.empty()) return;

	m_queues = std::make_shared<StageQueues>();
	m_queues->onBatchReady = std::move(onBatchReady);

	for (const auto& [sceneID, relativePath] : AssetPaths)
	{
		std::filesystem::path fullPath =
			std::filesystem::path(BaseAssetPath) / relativePath;

		auto context      = std::make_shared<GLTFJobContext>();
		context->batch    = std::make_shared<SceneUploadBatch>();
		context->basePath = fullPath.parent_path();

		context->batch->sceneID   = sceneID;
		context->batch->sceneName = relativePath;

		m_queues->loadFile.Push(context);
		m_queues->pendingSceneCount++;
	}

	auto queues = m_queues;

	// Workers drain all CPU stages
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
		const std::string fullPath = BaseAssetPath + context->batch->sceneName;
		fmt::println("[AssetManager] Loading: {}", fullPath);

		fastgltf::Parser parser;
		auto data = fastgltf::GltfDataBuffer::FromPath(fullPath);
		if (!data || data.error() != fastgltf::Error::None)
		{
			fmt::println("[AssetManager] Failed to read: {}", fullPath);
			sq->pendingSceneCount--;
			continue;
		}

		constexpr auto opts =
			fastgltf::Options::DontRequireValidAssetMember |
			fastgltf::Options::AllowDouble |
			fastgltf::Options::LoadGLBBuffers |
			fastgltf::Options::LoadExternalBuffers |
			fastgltf::Options::LoadExternalImages;

		auto type = fastgltf::determineGltfFileType(data.get());
		fastgltf::Expected<fastgltf::Asset> result{ fastgltf::Error::None };

		if (type == fastgltf::GltfType::glTF)
			result = parser.loadGltf(data.get(), context->basePath, opts);
		else if (type == fastgltf::GltfType::GLB)
			result = parser.loadGltfBinary(data.get(), context->basePath, opts);
		else
		{
			fmt::println("[AssetManager] Unknown file type: {}", fullPath);
			sq->pendingSceneCount--;
			continue;
		}

		if (!result || result.error() != fastgltf::Error::None)
		{
			fmt::println("[AssetManager] Parse failed: {}", fullPath);
			sq->pendingSceneCount--;
			continue;
		}

		context->gltfAsset = std::move(result.get());
		sq->Advance(context, GLTFJobType::LoadFile);
		++ctx.jobsExecuted;
	}
	return true;
}

// -----------------------------------------------------------------------
// StageDecodeImages
// -----------------------------------------------------------------------

bool AssetManager::StageDecodeImages(ThreadContext& ctx)
{
	auto* sq = static_cast<StageQueues*>(ctx.workQueueActive);
	auto jobs = sq->decodeImages.Collect();
	if (jobs.empty()) return false;

	for (auto& context : jobs)
	{
		if (!context->CanRun(GLTFJobType::DecodeImages))
		{
			sq->decodeImages.Push(context);
			continue;
		}

		auto& gltf  = context->gltfAsset;
		auto& batch = *context->batch;
		batch.textures.reserve(gltf.images.size());

		for (size_t i = 0; i < gltf.images.size(); ++i)
		{
			auto& gltfImage = gltf.images[i];
			TextureDesc desc{};

			// Resolve debug name
			if (!gltfImage.name.empty())
				desc.debugName = std::string(gltfImage.name);
			else if (std::holds_alternative<fastgltf::sources::URI>(gltfImage.data))
				desc.debugName = std::string(
					std::get<fastgltf::sources::URI>(gltfImage.data).uri.path());
			else
				desc.debugName = fmt::format("tex_{}_{}", batch.sceneName, i);

			desc.isSRGB = IsSRGBTexture(desc.debugName);

			// Decode pixels
			int w = 0, h = 0, ch = 0;
			uint8_t* raw = nullptr;

			std::visit(fastgltf::visitor
			{
				[&](fastgltf::sources::Array& arr)
				{
					raw = stbi_load_from_memory(
						reinterpret_cast<const uint8_t*>(arr.bytes.data()),
						static_cast<int>(arr.bytes.size()),
						&w, &h, &ch, 4);
				},
				[&](fastgltf::sources::URI& uri)
				{
					ASSERT(uri.uri.isLocalPath());
					auto full = context->basePath / std::string(uri.uri.path());
					raw = stbi_load(full.string().c_str(), &w, &h, &ch, 4);
				},
				[&](fastgltf::sources::BufferView& bv)
				{
					auto& bufView = gltf.bufferViews[bv.bufferViewIndex];
					auto& buf     = gltf.buffers[bufView.bufferIndex];
					std::visit(fastgltf::visitor
					{
						[&](fastgltf::sources::Array& arr)
						{
							raw = stbi_load_from_memory(
								reinterpret_cast<const uint8_t*>(arr.bytes.data()) + bufView.byteOffset,
								static_cast<int>(bufView.byteLength),
								&w, &h, &ch, 4);
						},
						[](auto&) {}
					}, buf.data);
				},
				[](auto&) {}
			}, gltfImage.data);

			if (raw && w > 0 && h > 0)
			{
				desc.width     = static_cast<uint32_t>(w);
				desc.height    = static_cast<uint32_t>(h);
				desc.needsMips = (w >= 8 && h >= 8);
				desc.pixelData.assign(raw, raw + (w * h * 4));
				stbi_image_free(raw);
			}
			else
			{
				// Empty pixelData signals Renderer to use checkerboard fallback
				fmt::println("[AssetManager] Decode failed: {}", desc.debugName);
			}

			batch.textures.push_back(std::move(desc));
		}

		sq->Advance(context, GLTFJobType::DecodeImages);
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
		if (!context->CanRun(GLTFJobType::BuildSamplers))
		{
			sq->buildSamplers.Push(context);
			continue;
		}

		auto& gltf  = context->gltfAsset;
		auto& batch = *context->batch;
		batch.samplers.reserve(gltf.samplers.size());

		for (auto& gltfSampler : gltf.samplers)
			batch.samplers.push_back(ExtractSamplerDesc(gltfSampler));

		sq->Advance(context, GLTFJobType::BuildSamplers);
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
		if (!context->CanRun(GLTFJobType::ProcessMaterials))
		{
			sq->processMaterials.Push(context);
			continue;
		}

		auto& gltf  = context->gltfAsset;
		auto& batch = *context->batch;

		// Always emit at least one default material at index 0
		// Default ids defined later
		{
			batch.materials.emplace_back(MaterialDesc{
				.flags = MATERIAL_FLAG_CASTS_SHADOWS,
				.passType = static_cast<uint32_t>(MaterialPass::Opaque)});
		}

		for (auto& mat : gltf.materials)
		{
			MaterialDesc desc{};
			desc.flags   |= MATERIAL_FLAG_CASTS_SHADOWS;
			desc.passType = static_cast<uint32_t>(MaterialPass::Opaque);

			auto resolveTexture = [&](
				const fastgltf::TextureInfo& info,
				uint32_t& outTexIdx,
				uint32_t& outSampIdx)
			{
				const auto& tex = gltf.textures[info.textureIndex];
				if (tex.imageIndex.has_value())
					outTexIdx = static_cast<uint32_t>(tex.imageIndex.value());
				if (tex.samplerIndex.has_value())
					outSampIdx = static_cast<uint32_t>(tex.samplerIndex.value());
			};

			if (mat.pbrData.baseColorTexture.has_value())
			{
				resolveTexture(*mat.pbrData.baseColorTexture,
					desc.albedoTexIdx, desc.albedoSamplerIdx);
				desc.colorFactor = glm::make_vec4(mat.pbrData.baseColorFactor.data());
			}
			if (mat.pbrData.metallicRoughnessTexture.has_value())
			{
				resolveTexture(*mat.pbrData.metallicRoughnessTexture,
					desc.metalRoughTexIdx, desc.metalRoughSampIdx);
				desc.metalRoughFactors = {
					mat.pbrData.metallicFactor,
					mat.pbrData.roughnessFactor
				};
			}
			if (mat.normalTexture.has_value())
			{
				resolveTexture(*mat.normalTexture,
					desc.normalTexIdx, desc.normalSamplerIdx);
				desc.normalScale  = mat.normalTexture->scale;
				desc.flags       |= MATERIAL_FLAG_HAS_NORMAL_MAP;
			}
			if (mat.emissiveTexture.has_value())
			{
				resolveTexture(*mat.emissiveTexture,
					desc.emissiveTexIdx, desc.emissiveSampIdx);
				desc.emissiveColor    = glm::make_vec3(mat.emissiveFactor.data());
				desc.emissiveStrength = mat.emissiveStrength;
			}

			if (mat.alphaMode == fastgltf::AlphaMode::Mask)
			{
				desc.alphaCutoff  = mat.alphaCutoff != 0.0f ? mat.alphaCutoff : 0.5f;
				desc.flags       |= MATERIAL_FLAG_ALPHA_MASKED;
			}
			if (mat.alphaMode == fastgltf::AlphaMode::Blend)
			{
				desc.passType  = static_cast<uint32_t>(MaterialPass::Transparent);
				desc.flags    &= ~MATERIAL_FLAG_CASTS_SHADOWS;
			}
			if (IsTreeMaterial(mat))
				desc.flags |= MATERIAL_FLAG_IS_TREE;

			batch.materials.push_back(std::move(desc));
		}

		batch.materialFlags.reserve(batch.materials.size());
		for (const auto& desc : batch.materials)
			batch.materialFlags.push_back(desc.flags);

		sq->Advance(context, GLTFJobType::ProcessMaterials);
		++ctx.jobsExecuted;
	}
	return true;
}

// -----------------------------------------------------------------------
// StageProcessMeshes
// -----------------------------------------------------------------------

bool AssetManager::StageProcessMeshes(ThreadContext& ctx)
{
	auto* sq = static_cast<StageQueues*>(ctx.workQueueActive);
	auto jobs = sq->processMeshes.Collect();
	if (jobs.empty()) return false;

	auto& rawVerts  = ctx.scratch.bufferA;
	auto& rawIdx    = ctx.scratch.bufferB;
	auto& rawLodIdx = ctx.scratch.bufferC;

	for (auto& context : jobs)
	{
		if (!context->CanRun(GLTFJobType::ProcessMeshes))
		{
			sq->processMeshes.Push(context);
			continue;
		}

		auto& gltf  = context->gltfAsset;
		auto& batch = *context->batch;

		// --- Count pass: SAME predicate as fill (POSITION + indices) ---
		size_t totalVerts = 0, totalIndices = 0;
		for (auto& node : gltf.nodes)
		{
			if (!node.meshIndex.has_value()) continue;
			for (auto& prim : gltf.meshes[*node.meshIndex].primitives)
			{
				auto pos = prim.findAttribute("POSITION");
				if (pos == prim.attributes.end())      continue;
				if (!prim.indicesAccessor.has_value()) continue;
				totalVerts   += gltf.accessors[pos->accessorIndex].count;
				totalIndices += gltf.accessors[*prim.indicesAccessor].count;
			}
		}

		batch.vertices.resize(totalVerts);   // default-zeroed Vertex{}
		batch.indices.resize(totalIndices);

		size_t vtxCursor = 0, idxCursor = 0;

		for (uint32_t nodeIdx = 0; nodeIdx < static_cast<uint32_t>(gltf.nodes.size()); ++nodeIdx)
		{
			auto& node = gltf.nodes[nodeIdx];
			if (!node.meshIndex.has_value()) continue;

			for (auto& prim : gltf.meshes[*node.meshIndex].primitives)
			{
				auto posAttr = prim.findAttribute("POSITION");
				if (posAttr == prim.attributes.end())  continue;
				if (!prim.indicesAccessor.has_value()) continue;

				const auto& posAcc = gltf.accessors[posAttr->accessorIndex];
				const auto& idxAcc = gltf.accessors[*prim.indicesAccessor];
				const uint32_t vCnt = static_cast<uint32_t>(posAcc.count);
				const uint32_t iCnt = static_cast<uint32_t>(idxAcc.count);

				// --- Fill: position written straight into the vertex ---
				fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAcc,
					[&](glm::vec3 v, size_t i)
					{
						batch.vertices[vtxCursor + i].position = v;
					});

				if (auto it = prim.findAttribute("NORMAL"); it != prim.attributes.end())
					fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[it->accessorIndex],
						[&](glm::vec3 v, size_t i)
						{
							EncodeOctahedral_Normal(batch.vertices[vtxCursor + i], v);
						});

				if (auto it = prim.findAttribute("TANGENT"); it != prim.attributes.end())
					fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[it->accessorIndex],
						[&](glm::vec4 v, size_t i)
						{
							EncodeOctahedral_Tangent(batch.vertices[vtxCursor + i], v);});

				if (auto it = prim.findAttribute("TEXCOORD_0"); it != prim.attributes.end())
					fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[it->accessorIndex],
						[&](glm::vec2 v, size_t i)
						{
							batch.vertices[vtxCursor + i].uvX = FloatToHalfBits(v.x);
							batch.vertices[vtxCursor + i].uvY = FloatToHalfBits(v.y);
						});

				if (auto it = prim.findAttribute("COLOR_0"); it != prim.attributes.end())
					fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[it->accessorIndex],
						[&](glm::vec4 v, size_t i)
						{
							EncodeRGBA8(batch.vertices[vtxCursor + i], v);
						});

				fastgltf::iterateAccessorWithIndex<uint32_t>(gltf, idxAcc,
					[&](uint32_t idx, size_t j)
					{
						batch.indices[idxCursor + j] = idx;
					});

				uint32_t* idxPtr = batch.indices.data()  + idxCursor;
				Vertex*   vtxPtr = batch.vertices.data() + vtxCursor;

				// Position read straight from inside the vertex (float32), stride = sizeof(Vertex).
				// meshopt reorders the vertex (and position with it) in lockstep, so no separate
				// position array and no remap desync — this is the watertight path.
				const float* posPtr = reinterpret_cast<const float*>(
					reinterpret_cast<const uint8_t*>(vtxPtr) + offsetof(Vertex, position));

				// --- AABB + scale from the vertex positions [0, vCnt) ---
				glm::vec3 vmin = vtxPtr[0].position, vmax = vmin;
				for (uint32_t i = 0; i < vCnt; ++i)
				{
					vmin = glm::min(vmin, vtxPtr[i].position);
					vmax = glm::max(vmax, vtxPtr[i].position);
				}
				const float simplifyScale = meshopt_simplifyScale(posPtr, vCnt, sizeof(Vertex));

				// --- meshopt reorder ---
				meshopt_optimizeVertexCache(idxPtr, idxPtr, iCnt, vCnt);
				if ((iCnt % 3u) == 0u)
					meshopt_optimizeOverdraw(idxPtr, idxPtr, iCnt, posPtr, vCnt, sizeof(Vertex), 1.05f);

				// optimizeVertexFetch reorders the vertex buffer AND remaps indices in lockstep;
				// float position lives in the vertex so it moves with it — fully consistent.
				rawVerts.resize(vCnt * sizeof(Vertex));
				meshopt_optimizeVertexFetch(
					reinterpret_cast<Vertex*>(rawVerts.data()),
					idxPtr, iCnt, vtxPtr, vCnt, sizeof(Vertex));
				std::memcpy(vtxPtr, rawVerts.data(), vCnt * sizeof(Vertex));
				// posPtr still valid — points into vtxPtr, now holding reordered vertices.

				// --- MeshDesc ---
				MeshDesc meshDesc{};
				meshDesc.firstIndex     = static_cast<uint32_t>(idxCursor);
				meshDesc.indexCount     = iCnt;
				meshDesc.vertexOffset   = static_cast<uint32_t>(vtxCursor);
				meshDesc.vertexCount    = vCnt;
				meshDesc.localAABB.vmin = vmin;
				meshDesc.localAABB.vmax = vmax;

				const uint32_t lod0Idx = static_cast<uint32_t>(batch.meshes.size());
				meshDesc.lod0 = lod0Idx;
				batch.meshes.push_back(meshDesc);

				// --- Resolve material/pass ---
				uint32_t passType = static_cast<uint32_t>(MaterialPass::Opaque);
				uint32_t matFlags = 0;
				uint32_t localMaterialIdx = DEFAULT_MATERIAL_INDEX;
				if (prim.materialIndex.has_value())
				{
					localMaterialIdx = static_cast<uint32_t>(prim.materialIndex.value()) + 1;
					ASSERT(localMaterialIdx < batch.materials.size()
						&& "glTF material index out of range of processed materials.");
					passType = batch.materials[localMaterialIdx].passType;
					matFlags = batch.materials[localMaterialIdx].flags;
				}

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

					MeshDesc lodMesh   = meshDesc;
					lodMesh.firstIndex = static_cast<uint32_t>(batch.indices.size());
					lodMesh.indexCount = static_cast<uint32_t>(lodCount);
					lodMesh.flags     |= MESH_FLAG_IS_LOD;
					const uint32_t lodIdx = static_cast<uint32_t>(batch.meshes.size());
					batch.indices.insert(batch.indices.end(), lodIdxPtr, lodIdxPtr + lodCount);
					batch.meshes.push_back(lodMesh);
					return lodIdx;
				};

				if ((iCnt % 3u) == 0u)
				{
					uint32_t lod1 = buildLOD(0.60f, 0.005f);
					uint32_t lod2 = buildLOD(0.35f, 0.010f);
					uint32_t lod3 = buildLOD(0.20f, 0.020f);

					auto& md = batch.meshes[lod0Idx];
					md.lod1 = lod1 != UINT32_MAX ? lod1 : lod0Idx;
					md.lod2 = lod2 != UINT32_MAX ? lod2 : md.lod1;
					md.lod3 = lod3 != UINT32_MAX ? lod3 : md.lod2;

					const bool thinMesh = MeshRegistry::IsThinMeshForShadows(
						Mesh{ .localAABB = { vmin, vmax } });

					// Pin shadow-LOD0 for geometry where simplification leaks light:
					// thin slivers, tiny meshes, and alpha-tested foliage.
					const bool isFoliage = (matFlags & MATERIAL_FLAG_ALPHA_MASKED)
										 || (matFlags & MATERIAL_FLAG_IS_TREE);

					const bool forceLod0 = thinMesh || iCnt < 300u || isFoliage;

					if (forceLod0)
					{
						md.shadowLod0 = lod0Idx;
						md.shadowLod1 = lod0Idx;
						md.shadowLod2 = lod0Idx;
						md.flags |= MESH_LOD_FLAG_FORCE_SHADOW_LOD0;
					}
					else
					{
						md.shadowLod0 = lod0Idx;
						md.shadowLod1 = buildLOD(0.40f, 0.006f);
						md.shadowLod2 = buildLOD(0.18f, 0.012f);
						if (md.shadowLod1 == UINT32_MAX) md.shadowLod1 = lod0Idx;
						if (md.shadowLod2 == UINT32_MAX) md.shadowLod2 = md.shadowLod1;
					}
				}

				batch.instances.emplace_back(InstanceDesc{
					.localMeshIdx = lod0Idx,
					.localMaterialIdx = localMaterialIdx,
					.nodeIdx = nodeIdx,
					.passType = passType});

				vtxCursor += vCnt;
				idxCursor += iCnt;
			}
		}

		// Shadow index buffer.
		// Base (lod0) meshes get a position-welded shadow copy appended after the render
		// indices. LOD meshes are ALREADY simplified — welding them again tears their
		// silhouette (the leak). LODs reuse their own render indices.
		const size_t shadowBase = batch.indices.size();

		// Reserve shadow space only for base meshes (sum of their index counts).
		size_t shadowTotal = 0;
		for (const auto& md : batch.meshes)
			if ((md.flags & MESH_FLAG_IS_LOD) == 0u)
				shadowTotal += md.indexCount;

		batch.indices.resize(shadowBase + shadowTotal);

		size_t shadowCursor = shadowBase;
		for (auto& md : batch.meshes)
		{
			if (md.flags & MESH_FLAG_IS_LOD)
			{
				// Reuse the LOD's own (simplified, watertight) render indices for shadows.
				md.shadowFirstIndex = md.firstIndex;
				md.shadowIndexCount = md.indexCount;
				continue;
			}

			md.shadowFirstIndex = static_cast<uint32_t>(shadowCursor);
			md.shadowIndexCount = md.indexCount;

			// Float position stream for the position-only weld.
			const Vertex* verts = batch.vertices.data() + md.vertexOffset;
			meshopt_Stream streams[1]{};
			streams[0].data   = reinterpret_cast<const uint8_t*>(verts) + offsetof(Vertex, position);
			streams[0].size   = sizeof(float) * 3u;
			streams[0].stride = sizeof(Vertex);

			meshopt_generateShadowIndexBufferMulti(
				batch.indices.data() + md.shadowFirstIndex,
				batch.indices.data() + md.firstIndex,
				md.shadowIndexCount, md.vertexCount, streams, 1u);

			shadowCursor += md.indexCount;
		}

		sq->Advance(context, GLTFJobType::ProcessMeshes);
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
		if (!context->CanRun(GLTFJobType::BuildSceneGraph))
		{
			sq->buildSceneGraph.Push(context);
			continue;
		}

		auto& gltf  = context->gltfAsset;
		auto& batch = *context->batch;

		// Build node list
		std::vector<std::shared_ptr<Node>> nodes;
		nodes.reserve(gltf.nodes.size());

		for (auto& srcNode : gltf.nodes)
		{
			auto node = std::make_shared<Node>();
			std::visit(fastgltf::visitor
			{
				[&](const fastgltf::math::fmat4x4& m)
				{
					node->localTransform = glm::make_mat4x4(m.data());
				},
				[&](const fastgltf::TRS& trs)
				{
					glm::vec3 t(trs.translation[0], trs.translation[1], trs.translation[2]);
					glm::quat r(trs.rotation[3], trs.rotation[0], trs.rotation[1], trs.rotation[2]);
					glm::vec3 s(trs.scale[0], trs.scale[1], trs.scale[2]);
					node->localTransform =
						glm::translate(glm::mat4(1.f), t) *
						glm::toMat4(r) *
						glm::scale(glm::mat4(1.f), s);
				}
			}, srcNode.transform);
			nodes.push_back(node);
		}

		// Parent-child wiring
		for (size_t i = 0; i < gltf.nodes.size(); ++i)
			for (auto childIdx : gltf.nodes[i].children)
			{
				nodes[i]->children.push_back(nodes[childIdx]);
				nodes[childIdx]->parent = nodes[i];
			}

		// World transforms
		for (auto& node : nodes)
			if (node->parent.expired())
				node->RefreshTransform(glm::mat4(1.f));

		// Collect unique nodes referenced by instances and build transform list
		std::unordered_map<uint32_t, uint32_t> nodeToSlot;
		std::vector<uint32_t> uniqueNodeIDs;

		for (auto& inst : batch.instances)
		{
			if (nodeToSlot.find(inst.nodeIdx) == nodeToSlot.end())
			{
				const uint32_t slot = static_cast<uint32_t>(uniqueNodeIDs.size());
				nodeToSlot[inst.nodeIdx] = slot;
				uniqueNodeIDs.push_back(inst.nodeIdx);
			}
		}

		batch.localToNodeSlot.resize(batch.instances.size());
		for (size_t i = 0; i < batch.instances.size(); ++i)
		{
			const uint32_t nodeIdx = batch.instances[i].nodeIdx;
			auto it = nodeToSlot.find(nodeIdx);
			ASSERT(it != nodeToSlot.end());
			batch.localToNodeSlot[i] = it->second;
		}

		batch.nodeTransforms.reserve(uniqueNodeIDs.size());
		for (auto nodeIdx : uniqueNodeIDs)
			batch.nodeTransforms.push_back(nodes[nodeIdx]->worldTransform);

		VirtualInstance vi{};
		vi.sceneID           = static_cast<uint8_t>(batch.sceneID);
		vi.perInstanceStride = static_cast<uint32_t>(batch.instances.size());
		vi.transformCount    = static_cast<uint32_t>(uniqueNodeIDs.size());
		vi.baseTransform     = batch.nodeTransforms.empty()
							   ? glm::mat4(1.0f) : batch.nodeTransforms[0];

		batch.virtualInstance = vi;

		sq->Advance(context, GLTFJobType::BuildSceneGraph);
		++ctx.jobsExecuted;
	}
	return true;
}

// -----------------------------------------------------------------------
// Cleanup
// -----------------------------------------------------------------------

void AssetManager::Shutdown(JobSystem& jobSystem)
{
	// Drain any in-flight CPU pipeline stages
	jobSystem.Wait();

	// Drop queue — all contexts have completed
	m_queues.reset();

	// Clear any pending batches that never got consumed
	{
		std::scoped_lock lock(m_batchMutex);
		m_pendingBatches.clear();
	}
}
