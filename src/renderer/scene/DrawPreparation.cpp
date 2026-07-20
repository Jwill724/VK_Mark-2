#include "pch.h"

#include "DrawPreparation.h"
#include "Material.h"
#include "Mesh.h"
#include "../backend/Device.h"
#include "../backend/memory/ResourceAllocator.h"
#include "../backend/BufferBarriers.h"
#include "../../core/AssetUploadTypes.h"
#include "../../common/ResourceTypes.h"
#include "../frame/FrameContext.h"
#include "renderer/RendererDefinitions.h"

namespace RD = RendererDefinitions;

static constexpr uint32_t TransformIDFor(const VirtualInstance& gi, uint32_t copy, uint32_t localSlot) {
	return gi.firstTransform + copy * gi.transformCount + localSlot;
}

static constexpr bool IsDynamicMethod(const VirtualInstance& gi)
{
	return gi.instancingMethod == RD::InstancingMethod::DrawDynamic ||
	       gi.instancingMethod == RD::InstancingMethod::DrawMultiDynamic;
}

// Creates the initial rows (mesh X copies)
static void BakeInstanceData(
	InstanceState& vs,
	const VirtualInstance& gi,
	const ModelAsset& asset,
	const std::vector<Mesh>& meshData,
	const std::vector<MeshLODs>& meshLods,
	const std::vector<glm::mat4>& transforms,
	const std::vector<uint32_t>&  materialFlags,
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

	outFirst = static_cast<uint32_t>(vs.gpuInputs.size());
	outCount = copies * stride;

	const size_t newSize = static_cast<size_t>(outFirst + outCount);
	vs.gpuInputs.resize(newSize);

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

			MeshLODs lods = meshLods[meshID];

			const uint32_t matID    = instDesc.localMaterialIdx;
			const uint32_t matFlags = (matID < materialFlags.size()) ? materialFlags[matID] : 0u;

			uint32_t flags = 0;

			// --- Layer 1: pass routing (from primitive) ---
			if (instDesc.passType == static_cast<uint32_t>(MaterialPass::Opaque))
				flags |= InstanceFlags::PASS_OPAQUE;
			else
				flags |= InstanceFlags::PASS_TRANSPARENT;

			// --- Layer 1: material-driven ---
			if (matFlags & MATERIAL_FLAG_ALPHA_MASKED)
				flags |= InstanceFlags::ALPHA_TESTED;

			if (matFlags & MATERIAL_FLAG_IS_TREE)
				flags |= InstanceFlags::IS_TREE;

			if (matFlags & MATERIAL_FLAG_HAS_NORMAL_MAP)
				flags |= InstanceFlags::HAS_NORMALS;

			// shadow casting — opaque only, stripped from blend materials
			if ((flags & InstanceFlags::PASS_OPAQUE) &&
				(matFlags & MATERIAL_FLAG_CASTS_SHADOWS))
			{
				flags |= InstanceFlags::CAST_CSM;
				flags |= InstanceFlags::CAST_FLASHLIGHT;
			}

			// shadow receiving — all opaque geometry
			if (flags & InstanceFlags::PASS_OPAQUE)
				flags |= InstanceFlags::RECEIVE_SHADOW;

			// --- Layer 1: mesh-driven ---
			if (lods.flags & MESH_FLAG_IS_LOD)
				flags |= InstanceFlags::LOD_ENABLED;

			if (lods.flags & MESH_FLAG_GOOD_OCCLUDEE)
				flags |= InstanceFlags::OCCLUDABLE;

			// --- Layer 2: placement-driven ---
			if (gi.instancingMethod == RD::InstancingMethod::DrawStatic ||
				gi.instancingMethod == RD::InstancingMethod::DrawMultiStatic)
				flags |= InstanceFlags::STATIC_OBJECT;
			else
				flags |= InstanceFlags::DYNAMIC_OBJECT;

			// all instances start active; lazy shrink clears this
			flags |= InstanceFlags::INSTANCE_ACTIVE;

			// --- Layer 3: runtime override from VirtualInstance ---
			flags = (flags & gi.flagsMask) | gi.flagsForce;

			InstanceInput row{};
			row.meshID      = meshID;
			row.materialID  = instDesc.localMaterialIdx;
			row.transformID = transformID;
			row.lod0        = lods.lod0;
			row.lod1        = lods.lod1;
			row.lod2        = lods.lod2;
			row.lod3        = lods.lod3;
			row.shadowLod0  = lods.shadowLod0;
			row.shadowLod1  = lods.shadowLod1;
			row.shadowLod2  = lods.shadowLod2;
			row.flags       = flags;

			vs.gpuInputs[writeIndex] = row;
		}
	}

	vs.slabs[static_cast<ModelID>(gi.sceneID)] = {
		.first      = outFirst,
		.stride     = stride,
		.usedCopies = copies
	};
}

// Toggles INSTANCE_ACTIVE when a dynamic placement's copy count changes
// Returns true if any row changed (caller must re-upload instance inputs).
static bool SyncCopyCount(InstanceState& vs, CoreSlab& slab, const VirtualInstance& gi)
{
	if (slab.usedCopies == gi.usedCopies)
		return false;

	ASSERT(gi.usedCopies <= gi.capacityCopies);

	const uint32_t stride = slab.stride;

	for (uint32_t c = 0; c < gi.capacityCopies; ++c)
	{
		const bool shouldBeActive = (c < gi.usedCopies);
		uint32_t idx = slab.first + c * stride;

		for (uint32_t local = 0; local < stride; ++local, ++idx)
		{
			uint32_t& flags = vs.gpuInputs[idx].flags;

			if (shouldBeActive)
				flags |= InstanceFlags::INSTANCE_ACTIVE;
			else
				flags &= ~InstanceFlags::INSTANCE_ACTIVE;
		}
	}

	slab.usedCopies = gi.usedCopies;
	return true;
}

//static bool UpdateWorldAABBsForDynamic(
//	InstanceState& vs,
//	const VirtualInstance& gi,
//	const std::vector<Mesh>& meshData,
//	const std::vector<glm::mat4>& transforms)
//{
//	if (!IsDynamicMethod(gi))
//		return false;
//
//	const auto it = vs.slabs.find(static_cast<ModelID>(gi.sceneID));
//	if (it == vs.slabs.end()) return false;
//
//	const CoreSlab& slab = it->second;
//	if (slab.usedCopies == 0) return false;
//
//	// Only the active (used) copies need fresh AABBs; inactive tail rows are
//	// gated out of the active list and by INSTANCE_ACTIVE anyway.
//	const uint32_t rowCount = slab.usedCopies * slab.stride;
//	uint32_t idx = slab.first;
//
//	for (uint32_t i = 0; i < rowCount; ++i, ++idx)
//	{
//		const uint32_t meshID = vs.gpuInputs[idx].meshID;
//		const uint32_t tid    = vs.gpuInputs[idx].transformID;
//
//		ASSERT(meshID < meshData.size());
//		ASSERT(tid < transforms.size());
//
//		vs.worldAABBs[idx] = AABBtoWorldSpace(
//			meshData[meshID].localAABB, transforms[tid]);
//	}
//
//	return rowCount > 0;
//}

static void RebuildActive(InstanceState& vs)
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

static uint32_t RegisterBin(BinTableBuild& table, uint32_t meshID, uint32_t materialID)
{
	uint32_t h = BinHash(meshID, materialID);

	for (uint32_t probe = 0; probe < RD::BIN_TABLE_SIZE; ++probe)
	{
		BinKey& slot = table.binKeys.hashTable[h];

		if (slot.meshID == meshID && slot.materialID == materialID) return slot.binID;

		if (slot.meshID == RD::INVALID_U32)
		{
			ASSERT(table.binCount < RD::MAX_DRAW_BINS, "Exceeded MAX_DRAW_BINS");

			slot.meshID     = meshID;
			slot.materialID = materialID;
			slot.binID      = table.binCount;

			table.binKeys.denseKeys[table.binCount] = { meshID, materialID };
			return table.binCount++;
		}

		h = (h + 1u) & (RD::BIN_TABLE_SIZE - 1u);
	}

	ASSERT(false, "Bin hash table full");
	return RD::INVALID_U32;
}

BinTableBuild DrawPreparation::BuildDrawBinTable(const std::vector<InstanceInput>& instances)
{
	BinTableBuild table;
	table.binKeys.hashTable.resize(RD::BIN_TABLE_SIZE, { RD::INVALID_U32, 0u, 0u });
	table.binKeys.denseKeys.resize(RD::MAX_DRAW_BINS, glm::uvec2(RD::INVALID_U32));

	for (const InstanceInput& inst : instances)
	{
		const uint32_t mat = inst.materialID;

		// primary LOD chain — exactly what selectLOD can return
		if (inst.flags & LOD_ENABLED)
		{
			RegisterBin(table, inst.lod0, mat);
			RegisterBin(table, inst.lod1, mat);
			RegisterBin(table, inst.lod2, mat);
			RegisterBin(table, inst.lod3, mat);
		}
		else
		{
			RegisterBin(table, inst.meshID, mat);
		}

		// shadow LOD chain — exactly what selectShadowLOD / flashlight can return
		if (inst.flags & (CAST_CSM | CAST_FLASHLIGHT))
		{
			RegisterBin(table, inst.shadowLod0, mat);
			RegisterBin(table, inst.shadowLod1, mat);
			RegisterBin(table, inst.shadowLod2, mat);
		}
	}

	return table;
}

bool DrawPreparation::SyncInstanceInputs(
	InstanceState& vs,
	const std::vector<VirtualInstance>& virtualInstances,
	const std::unordered_map<ModelID, std::shared_ptr<ModelAsset>>& loaded,
	const std::vector<Mesh>& meshData,
	const std::vector<MeshLODs>& meshLods,
	const std::vector<glm::mat4>& transforms,
	const std::vector<uint32_t>&  materialFlags)
{
	bool gpuInputsDirty = false;

	for (const auto& gi : virtualInstances)
	{
		const ModelID sid = static_cast<ModelID>(gi.sceneID);
		auto assetIt = loaded.find(sid);
		if (assetIt == loaded.end()) continue;

		const ModelAsset& asset = *assetIt->second;
		if (!asset.IsLoaded()) continue;

		ASSERT(gi.perInstanceStride == static_cast<uint32_t>(asset.instances.size()));

		auto slabIt = vs.slabs.find(sid);

		if (slabIt == vs.slabs.end())
		{
			// First sighting — bake full-capacity slab. For the asteroid field
			// this is every loaded copy, all pointing at dynamic transform slots.
			uint32_t f = 0, c = 0;
			BakeInstanceData(vs, gi, asset, meshData, meshLods, transforms, materialFlags, f, c);
			gpuInputsDirty = true;
			continue;
		}

		// Slab exists — keep it in sync with the live placement.
		CoreSlab& slab = slabIt->second;

		// Copy count moved (spawn/despawn within capacity)
		if (SyncCopyCount(vs, slab, gi)) gpuInputsDirty = true;

		//// Dynamic placements (asteroid field) get fresh world AABBs every frame
		//// from the simulation-updated transforms, so culling follows them.
		//UpdateWorldAABBsForDynamic(vs, gi, meshData, transforms);
	}

	if (gpuInputsDirty) RebuildActive(vs);

	return gpuInputsDirty;
}


void DrawPreparation::UploadGPUBuffersForFrame(
	FrameContext&                     frameCtx,
	BindlessBDATable&                 globalBDATable,
	const DrawBinKeys&                drawBinKeys,
	Device&                           device,
	Allocator&                        allocator,
	const std::vector<InstanceInput>& instanceInputs,
	const std::vector<glm::mat4>&     transforms,
	const std::vector<LocalLight>&    lights,
	bool                              isTemporalValid)
{
	auto& frameStaging = allocator.FrameStaging;

	auto& addrTable = frameCtx.GetBindlessBDATable();

	const bool uploadInstances  = frameCtx.IsInstanceInputsUploadNeeded();
	const bool uploadTransforms = frameCtx.IsTransformsUploadNeeded();
	const bool uploadLights     = frameCtx.IsLightsUploadNeeded();

	if (!uploadInstances && !uploadTransforms && !uploadLights && !addrTable.IsTableDirty()) return;

	// Stage everything into FrameStaging before recording the command
	struct UploadPlan
	{
		StagedWrite instanceInputs{};
		StagedWrite drawBinKeys{};
		StagedWrite drawBinKeysDense{};
		StagedWrite globalAddrTable{};
		StagedWrite transforms{};
		StagedWrite prevTransforms{};
		StagedWrite lights{};
		StagedWrite addrTable{}; // frame address table
		uint32_t addrVersion = UINT32_MAX;

		// Assumes draw bins and global table as well
		bool instanceUploadNeeded = false;

		bool hasTransforms  = false;
		bool hasPrevTf      = false;
		bool hasLights      = false;
		bool hasAddrTable   = false;
	} plan;

	// Global buffers for instance inputs and draw bin keys
	if (frameCtx.IsInstanceInputsUploadNeeded() &&
		!instanceInputs.empty() &&
		!drawBinKeys.hashTable.empty() &&
		!drawBinKeys.denseKeys.empty())
	{
		const size_t instBytes     = instanceInputs.size()        * sizeof(InstanceInput);
		const size_t hashBytes     = drawBinKeys.hashTable.size() * sizeof(BinKey);
		const size_t denseBytes    = drawBinKeys.denseKeys.size() * sizeof(glm::uvec2);

		VkBuffer binKeyBuf = globalBDATable.GetGPUBuffer(RD::Renderer_Buffer::DrawBinKeys).m_buffer;

		plan.instanceInputs = frameStaging.Stage(
			instanceInputs.data(),
			instBytes,
			globalBDATable.GetGPUBuffer(RD::Renderer_Buffer::InstanceInputs).m_buffer);

		// hash table at offset 0
		plan.drawBinKeys = frameStaging.Stage(
			drawBinKeys.hashTable.data(),
			hashBytes,
			binKeyBuf,
			0);

		plan.drawBinKeysDense = frameStaging.Stage(
			drawBinKeys.denseKeys.data(),
			denseBytes,
			binKeyBuf,
			hashBytes);  // offset = end of hash table

		plan.globalAddrTable = frameStaging.Stage(
			globalBDATable.GetAddrPtrTable().data(),
			globalBDATable.GPU_ADDRESS_TABLE_SIZE_GPU_BYTES,
			globalBDATable.GetTableBuffer().m_buffer);

		plan.instanceUploadNeeded = true;
	}

	// Transforms
	if (uploadTransforms && !transforms.empty())
	{
		const size_t tfBytes = transforms.size() * sizeof(glm::mat4);

		plan.transforms = frameStaging.Stage(
			transforms.data(), tfBytes,
			addrTable.GetGPUBuffer(RD::Renderer_Buffer::Transforms).m_buffer);

		plan.hasTransforms = true;

		// Copy current -> prev for temporal
		if (isTemporalValid)
		{
			// prev is a GPU->GPU copy recorded in-command
			plan.hasPrevTf = true;
		}
	}

	// Lights
	if (uploadLights && !lights.empty())
	{
		const size_t lightBytes = lights.size() * sizeof(LocalLight);

		plan.lights = frameStaging.Stage(
			lights.data(),
			lightBytes,
			addrTable.GetGPUBuffer(RD::Renderer_Buffer::Lights).m_buffer);

		plan.hasLights = true;
	}

	// Per-frame address table
	if (addrTable.IsTableDirty())
	{
		plan.addrTable = frameStaging.Stage(
			addrTable.GetAddrPtrTable().data(),
			addrTable.GPU_ADDRESS_TABLE_SIZE_GPU_BYTES,
			addrTable.GetTableBuffer().m_buffer);

		plan.hasAddrTable = true;
		plan.addrVersion = addrTable.GetCpuVersion();
	}

	frameStaging.Flush();

	// Record transfer command
	device.RecordDeferredCommand([&](VkCommandBuffer cmd)
	{
		if (plan.instanceUploadNeeded)
		{
			frameStaging.CopyCommand(cmd, plan.instanceInputs);
			frameStaging.CopyCommand(cmd, plan.drawBinKeys);
			frameStaging.CopyCommand(cmd, plan.drawBinKeysDense);
			frameStaging.CopyCommand(cmd, plan.globalAddrTable);

			BufferBarriers::TransferReleaseOnGraphics(
				cmd,
				globalBDATable.GetGPUBuffer(RD::Renderer_Buffer::InstanceInputs),
				device.GetContext());
			BufferBarriers::TransferReleaseOnGraphics(
				cmd,
				globalBDATable.GetGPUBuffer(RD::Renderer_Buffer::DrawBinKeys),
				device.GetContext());
			BufferBarriers::TransferReleaseOnGraphics(
				cmd,
				globalBDATable.GetTableBuffer(),
				device.GetContext());
		}

		if (plan.hasTransforms)
		{
			// GPU->GPU copy of old transforms into prevTransforms before overwrite
			if (plan.hasPrevTf)
			{
				const size_t tfBytes = transforms.size() * sizeof(glm::mat4);
				VkBufferCopy prevCopy{};
				prevCopy.size = tfBytes;
				vkCmdCopyBuffer(
					cmd,
					addrTable.GetGPUBuffer(RD::Renderer_Buffer::Transforms).m_buffer,
					addrTable.GetGPUBuffer(RD::Renderer_Buffer::PrevTransforms).m_buffer,
					1, &prevCopy);

				BufferBarriers::TransferReleaseOnGraphics(
					cmd,
					addrTable.GetGPUBuffer(RD::Renderer_Buffer::PrevTransforms),
					device.GetContext());
			}

			frameStaging.CopyCommand(cmd, plan.transforms);
			BufferBarriers::TransferReleaseOnGraphics(
				cmd,
				addrTable.GetGPUBuffer(RD::Renderer_Buffer::Transforms),
				device.GetContext());
		}

		if (plan.hasLights)
		{
			frameStaging.CopyCommand(cmd, plan.lights);
			BufferBarriers::TransferReleaseOnGraphics(
				cmd,
				addrTable.GetGPUBuffer(RD::Renderer_Buffer::Lights),
				device.GetContext());
		}

		if (plan.hasAddrTable)
		{
			frameStaging.CopyCommand(cmd, plan.addrTable);
			BufferBarriers::TransferReleaseOnGraphics(
				cmd,
				addrTable.GetTableBuffer(),
				device.GetContext());

			frameCtx.SetPendingAddrTableVersion(plan.addrVersion);
		}

	}, frameCtx.GetTransferPool(), QueueType::Transfer);

	frameCtx.CollectAndAppendCmds(std::move(device.DeferredCmds.CollectTransfer()), QueueType::Transfer);

	// Timeline semaphore sync — render waits on this value
	const uint64_t signalValue = device.GetTransferQueue().Submit(frameCtx.GetTransferCommands());

	frameCtx.StashSubmitted(QueueType::Transfer);
	frameCtx.GetTransferWaitValue() = signalValue;

	addrTable.SetGpuVersion(frameCtx.GetPendingAddrTableVersion());

	// Reset FrameStaging head — safe since transfer is submitted
	frameStaging.Reset();
}
