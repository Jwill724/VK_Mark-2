#include "pch.h"

#include "DrawPreparation.h"
#include "Material.h"
#include "Mesh.h"
#include "Bounds.h"
#include "../backend/Device.h"
#include "../backend/memory/ResourceAllocator.h"
#include "../backend/BufferBarriers.h"
#include "../frame/FrameContext.h"
#include "renderer/RendererDefinitions.h"

namespace RD = RendererDefinitions;

// -----------------------------------------------------------------------
// Internal batch helpers
// -----------------------------------------------------------------------

struct BatchKey
{
	uint32_t meshID;
	uint32_t materialID;
	bool operator==(const BatchKey& o) const
	{ return meshID == o.meshID && materialID == o.materialID; }
};
struct BatchKeyHash
{
	size_t operator()(const BatchKey& k) const
	{
		size_t h1 = std::hash<uint32_t>{}(k.meshID);
		size_t h2 = std::hash<uint32_t>{}(k.materialID);
		return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
	}
};

using BatchMap = std::unordered_map<BatchKey, std::vector<uint32_t>, BatchKeyHash>;

// LOD selection + batching into BatchMap
static void BuildBatches(
	BatchMap&                       outBatches,
	std::vector<Instance>&          outInstances,
	const std::vector<Instance>&    input,
	const std::vector<AABB>&        worldAABBs,
	const std::vector<MeshLODs>&    meshLods,
	const glm::vec3&                camPos,
	const glm::mat4&                proj)
{
	outInstances.clear();
	outInstances.reserve(input.size());

	for (uint32_t i = 0; i < static_cast<uint32_t>(input.size()); ++i)
	{
		Instance inst = input[i];

		if (inst.meshID < static_cast<uint32_t>(meshLods.size()))
		{
			const MeshLODs& lods     = meshLods[inst.meshID];
			const AABB& aabb         = worldAABBs[i];
			const glm::vec3 origin   = 0.5f * (aabb.vmin + aabb.vmax);
			const glm::vec3 extent   = 0.5f * (aabb.vmax - aabb.vmin);
			const float sphereRadius = glm::length(extent);
			float dist               = glm::length(origin - camPos) - sphereRadius;
			dist                     = std::max(0.0f, dist);

			const float projScaleY = proj[1][1];
			const float screenRadius = (sphereRadius * projScaleY) / dist;

			if      (screenRadius < 0.02f) inst.meshID = lods.lod3;
			else if (screenRadius < 0.05f) inst.meshID = lods.lod2;
			else if (screenRadius < 0.10f) inst.meshID = lods.lod1;
			else                           inst.meshID = lods.lod0;
		}

		const uint32_t idx = static_cast<uint32_t>(outInstances.size());
		outInstances.push_back(inst);
		outBatches[{inst.meshID, inst.materialID}].push_back(idx);
	}
}


DrawBuildOutput DrawPreparation::BuildAndSortIndirectDraws(
	const std::vector<Instance>&                                      inputVisible,
	const std::vector<AABB>&                                          worldAABBs,
	const std::vector<Mesh>&                                          meshes,
	const std::vector<MeshLODs>&                                      meshLods,
	const glm::vec4&                                                  cameraPos,
	const glm::mat4&                                                  cameraProj,
	const std::array<std::vector<Instance>, RD::MAX_SHADOW_CASCADES>& csmCasters,
	const std::vector<Instance>&                                      flashlightCasters,
	const std::vector<uint32_t>&                                      materialFlags,
	bool                                                              shadowsEnabled,
	bool                                                              flashlightOn)
{
	DrawBuildOutput out;
	ASSERT(!inputVisible.empty());

	const glm::vec3 camPos = glm::vec3(cameraPos);

	// LOD selection + dedup batching over camera-visible instances
	BatchMap batches;
	std::vector<Instance> lod_instances;
	BuildBatches(batches, lod_instances, inputVisible, worldAABBs, meshLods, camPos, cameraProj);

	// =========================================================
	// OPAQUE PASS
	// =========================================================
	{
		InstanceWriteScope instScope(out.visibleInstances);
		IndirectDrawScope  drawScope(out.indirectDraws);

		for (auto& [key, inds] : batches)
		{
			if (lod_instances[inds[0]].passType != static_cast<uint32_t>(MaterialPass::Opaque)) continue;

			if (key.meshID >= static_cast<uint32_t>(meshes.size())) continue;
			const Mesh& mesh = meshes[key.meshID];

			drawScope.Add({
				mesh.indexCount,
				static_cast<uint32_t>(inds.size()),
				mesh.firstIndex,
				static_cast<int32_t>(mesh.vertexOffset),
				static_cast<uint32_t>(out.visibleInstances.size())
			});
			for (uint32_t i : inds) instScope.Add(lod_instances[i]);
		}
		instScope.End();
		drawScope.End();

		out.opaqueInstances = instScope.GetRange();
		out.opaqueDraws     = drawScope.GetRange();
	}

	// =========================================================
	// TRANSPARENT PASS
	// =========================================================
	{
		InstanceWriteScope instScope(out.visibleInstances);
		IndirectDrawScope  drawScope(out.indirectDraws);

		for (auto& [key, inds] : batches)
		{
			if (lod_instances[inds[0]].passType !=
				static_cast<uint32_t>(MaterialPass::Transparent)) continue;

			if (key.meshID >= static_cast<uint32_t>(meshes.size())) continue;
			const Mesh& mesh = meshes[key.meshID];

			drawScope.Add({
				mesh.indexCount,
				static_cast<uint32_t>(inds.size()),
				mesh.firstIndex,
				static_cast<int32_t>(mesh.vertexOffset),
				static_cast<uint32_t>(out.visibleInstances.size())
			});
			for (uint32_t i : inds) instScope.Add(lod_instances[i]);
		}
		instScope.End();
		drawScope.End();

		out.transparentInstances = instScope.GetRange();
		out.transparentDraws     = drawScope.GetRange();
	}

	// =========================================================
	// CSM SHADOW PASSES
	// =========================================================
	if (shadowsEnabled)
	{
		for (uint32_t cascade = 0; cascade < RD::MAX_SHADOW_CASCADES; ++cascade)
		{
			const auto& casters = csmCasters[cascade];

			InstanceWriteScope instScope(out.visibleInstances);
			IndirectDrawScope  drawScope(out.indirectDraws);

			if (!casters.empty())
			{
				// Batch shadow casters by shadow mesh ID
				std::unordered_map<uint32_t, std::vector<uint32_t>> shadowBatches;
				shadowBatches.reserve(casters.size());

				for (uint32_t i = 0; i < static_cast<uint32_t>(casters.size()); ++i)
				{
					const Instance& inst = casters[i];
					uint32_t shadowID = inst.meshID;

					if (shadowID < static_cast<uint32_t>(meshLods.size()))
					{
						const MeshLODs& lods = meshLods[shadowID];
						uint32_t slot = MeshRegistry::GetShadowSlotForCascade(lods, cascade);

						// Apply foliage bias — alpha-masked materials use higher quality
						if (materialFlags[inst.materialID] & MATERIAL_FLAG_ALPHA_MASKED)
						{
							slot =  MeshRegistry::ApplyFoliageBias(slot, cascade);
						}

						shadowID = (slot == 0u) ? lods.shadowLod0
								 : (slot == 1u) ? lods.shadowLod1
												: lods.shadowLod2;
					}

					shadowBatches[shadowID].push_back(i);
				}

				for (auto& [shadowID, inds] : shadowBatches)
				{
					if (shadowID >= static_cast<uint32_t>(meshes.size())) continue;
					const Mesh& mesh = meshes[shadowID];

					drawScope.Add({
						mesh.shadowIndexCount,
						static_cast<uint32_t>(inds.size()),
						mesh.shadowFirstIndex,
						static_cast<int32_t>(mesh.vertexOffset),
						static_cast<uint32_t>(out.visibleInstances.size())
					});

					for (uint32_t i : inds)
					{
						Instance inst    = casters[i];
						inst.meshID      = shadowID;
						instScope.Add(inst);
					}
				}
			}

			instScope.End();
			drawScope.End();

			out.csm[cascade].instanceRange = instScope.GetRange();
			out.csm[cascade].drawRange     = drawScope.GetRange();
		}
	}

	// =========================================================
	// FLASHLIGHT SHADOW PASS
	// =========================================================
	{
		InstanceWriteScope instScope(out.visibleInstances);
		IndirectDrawScope  drawScope(out.indirectDraws);

		if (flashlightOn && !flashlightCasters.empty())
		{
			std::unordered_map<uint32_t, std::vector<uint32_t>> shadowBatches;
			shadowBatches.reserve(flashlightCasters.size());

			for (uint32_t i = 0; i < static_cast<uint32_t>(flashlightCasters.size()); ++i)
			{
				uint32_t shadowID = flashlightCasters[i].meshID;
				if (shadowID < static_cast<uint32_t>(meshLods.size()))
					shadowID = meshLods[shadowID].shadowLod0;
				shadowBatches[shadowID].push_back(i);
			}

			for (auto& [shadowID, inds] : shadowBatches)
			{
				if (shadowID >= static_cast<uint32_t>(meshes.size())) continue;
				const Mesh& mesh = meshes[shadowID];

				drawScope.Add({
					mesh.shadowIndexCount,
					static_cast<uint32_t>(inds.size()),
					mesh.shadowFirstIndex,
					static_cast<int32_t>(mesh.vertexOffset),
					static_cast<uint32_t>(out.visibleInstances.size())
				});

				for (uint32_t i : inds)
				{
					Instance inst = flashlightCasters[i];
					inst.meshID   = shadowID;
					instScope.Add(inst);
				}
			}
		}

		instScope.End();
		drawScope.End();

		out.flashlight.instanceRange = instScope.GetRange();
		out.flashlight.drawRange     = drawScope.GetRange();
	}

	return out;
}

void DrawPreparation::UploadGPUBuffersForFrame(
	FrameContext&                    frameCtx,
	Device&                          device,
	Allocator&                       allocator,
	const std::vector<glm::mat4>&    transforms,
	const std::vector<LocalLight>&   lights,
	bool                             isTemporalValid)
{
	auto& frameStaging = allocator.FrameStaging;

	const bool uploadInstances  = frameCtx.IsThereVisibles();
	const bool uploadTransforms = frameCtx.IsTransformsUploadNeeded();
	const bool uploadLights     = frameCtx.IsLightsUploadNeeded();

	if (!uploadInstances && !uploadTransforms && !uploadLights) return;

	// Stage everything into FrameStaging before recording the command
	struct UploadPlan
	{
		StagedWrite instances{};
		StagedWrite indirect{};
		StagedWrite transforms{};
		StagedWrite prevTransforms{};
		StagedWrite lights{};
		StagedWrite addrTable{};
		uint32_t addrVersion = UINT32_MAX;

		bool hasInstances   = false;
		bool hasTransforms  = false;
		bool hasPrevTf      = false;
		bool hasLights      = false;
		bool hasAddrTable   = false;
	} plan;

	auto& addrTable = frameCtx.GetBindlessBDATable();

	const auto& indirectDraws = frameCtx.GetIndirectCmds();

	// Instances + indirect draws
	if (uploadInstances)
	{
		const auto& visInst = frameCtx.GetVisibleInstances(); // full flat list
		const size_t instBytes = visInst.size() * sizeof(Instance);
		const size_t indBytes  = indirectDraws.size()
								 * sizeof(VkDrawIndexedIndirectCommand);

		plan.instances = frameStaging.Stage(
			visInst.data(), instBytes,
			addrTable.GetGPUBuffer(RD::Renderer_Buffer::VisibleInstances).m_buffer);

		plan.indirect = frameStaging.Stage(
			indirectDraws.data(), indBytes,
			addrTable.GetGPUBuffer(RD::Renderer_Buffer::IndirectDraws).m_buffer);

		plan.hasInstances = true;
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
		if (plan.hasInstances)
		{
			frameStaging.CopyCommand(cmd, plan.instances);
			frameStaging.CopyCommand(cmd, plan.indirect);

			BufferBarriers::TransferReleaseOnCompute(
				cmd,
				addrTable.GetGPUBuffer(RD::Renderer_Buffer::VisibleInstances),
				device.GetContext());
			BufferBarriers::TransferReleaseOnIndirect(
				cmd,
				addrTable.GetGPUBuffer(RD::Renderer_Buffer::IndirectDraws),
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
