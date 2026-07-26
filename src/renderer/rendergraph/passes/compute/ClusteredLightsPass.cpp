#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../../backend/BufferBarriers.h"
#include "EngineTypes.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../backend/memory/BindlessBDATable.h"
#include "../../../../profiler/Profiler.h"
#include "../../../frame/FrameContext.h"

namespace B = BufferBarriers;

static constexpr size_t PIPE_ID_LIGHT_CULL      = 0;
static constexpr size_t PIPE_ID_TILE_RANGES     = 1;
static constexpr size_t PIPE_ID_INDIRECT_ARGS   = 2;
static constexpr size_t PIPE_ID_CLUSTER_COUNTS  = 3;
static constexpr size_t PIPE_ID_CLUSTER_OFFSETS = 4;
static constexpr size_t PIPE_ID_CLUSTER_IDS     = 5;

void RegisterClusteredLightsPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Clustered_Lights",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.RunOnAsyncCompute()

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->InstancesActive() &&
							ctx.frameState->LightsActive() &&
							!ctx.frameState->DebugRenderFastPath();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::HiZ,
					RD::ImageAccess::ComputeRead)

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::ClusteredLights,
							pass.passName,
							ctx.threadSlot,
							ctx.scheduleInfo->queue);

						pass.scope = ComputeScope{{ ctx.frameState->GetLightCount(), 1u }, { WORKGROUP_256 }};
						auto& pso = std::get<ComputeScope>(pass.scope);

						const auto& frameCtx = ctx.frameCtx;
						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& indirectArgs        = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::DispatchIndirectArgs);
						const auto& tileSliceRanges     = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::ClusterTileSliceRanges);
						const auto& clusterCounts       = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::ClusterCounts);
						const auto& clusterCursors      = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::ClusterCursors);
						const auto& clusterScanScratch  = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::ClusterScanScratch);
						const auto& clusterOffsets      = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::ClusterOffsets);

						const auto& lightCountBuf       = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::VisibleLightCount);
						const auto& visibleLightIdsBuf  = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::VisibleLightIDs);

						const auto& hiZ       = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HiZ);
						const auto hiZSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::HiZ);

						const bool bAsync =
							ctx.scheduleInfo->queue == PassQueue::AsyncCompute;

						// Buffers reset to zero
						pso.FillGpuBuffer(cmd, clusterCursors);
						pso.FillGpuBuffer(cmd, clusterCounts);
						pso.FillGpuBuffer(cmd, clusterScanScratch);

						pso.FillGpuBuffer(cmd, lightCountBuf);
						B::CmdFillToComputeRW(cmd, lightCountBuf);

						// ==============
						// Light culling
						// ==============

						pso.DispatchComputePass(
							cmd,
							pass.pipelines[PIPE_ID_LIGHT_CULL],
							pass.pushWriter);

						B::ComputeWriteToRead(cmd, lightCountBuf);
						B::ComputeWriteToRead(cmd, visibleLightIdsBuf);


						// ==========================
						// Cluster Tile slice ranges
						// ==========================
						Extents2D clusterExtent = { ctx.frameCtx->GetClusterData().tileCountX, ctx.frameCtx->GetClusterData().tileCountY };
						pso.UpdateExtent(clusterExtent);
						pso.UpdateWorkgroups(WORKGROUP_8x8);

						// Only needed for tile slice ranges
						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							hiZ,
							hiZSampler);

						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_TILE_RANGES], pass.pushWriter);
						B::ComputeWriteToRead(cmd, tileSliceRanges);

						// =====================
						// Indirect arg compute
						// =====================

						pso.SetIndirect(indirectArgs.m_buffer, RD::DISPATCH_LIGHTS_OFFSET_BYTES);
						B::CmdFillToComputeRW(cmd, indirectArgs);
						pso.ClearIndirect();

						pso.UpdateWorkgroups(WORKGROUP_1);
						pso.UpdateExtent({1, 1});

						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_INDIRECT_ARGS], pass.pushWriter);
						B::ComputeWriteToIndirectRead(cmd, indirectArgs);

						// ===============
						// Cluster counts
						// ===============
						pso.SetIndirect(indirectArgs.m_buffer, RD::DISPATCH_LIGHTS_OFFSET_BYTES);
						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_CLUSTER_COUNTS], pass.pushWriter);
						B::ComputeWriteToRead(cmd, clusterCounts);

						// ================
						// Cluster offsets
						// ================
						pso.SetIndirect(indirectArgs.m_buffer, RD::DISPATCH_CLUSTERS_OFFSET_BYTES);
						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_CLUSTER_OFFSETS], pass.pushWriter);
						B::ComputeWriteToRead(cmd, clusterOffsets);

						// ====================
						// Cluster scatter ids
						// ====================
						pso.SetIndirect(indirectArgs.m_buffer, RD::DISPATCH_LIGHTS_OFFSET_BYTES);
						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_CLUSTER_IDS], pass.pushWriter);

						if (bAsync)
						{
							// Semaphore carries it to the fragment stage.
							B::ComputeWriteToRead(cmd, clusterScanScratch);
						}
						else
						{
							B::ComputeWriteToFragmentRead(cmd, clusterScanScratch);
						}
					});
		});
}
