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

static constexpr size_t PIPE_ID_TILE_RANGES     = 0;
static constexpr size_t PIPE_ID_INDIRECT_ARGS   = 1;
static constexpr size_t PIPE_ID_CLUSTER_COUNTS  = 2;
static constexpr size_t PIPE_ID_CLUSTER_OFFSETS = 3;
static constexpr size_t PIPE_ID_CLUSTER_IDS     = 4;

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
				.ReadResource(RD::Renderer_RenderTarget::HiZ,
					RD::ImageAccess::Read)

				.SetSetup(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						Extents2D clusterExtent = { ctx.frameCtx->GetClusterData().tileCountX, ctx.frameCtx->GetClusterData().tileCountY };
						pass.scope = ComputeScope{ clusterExtent, WORKGROUP_8x8 /* only for the first tile pass*/};
						auto& pso = std::get<ComputeScope>(pass.scope);

						const auto& hiZ = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HiZ);
						const auto hiZSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::HiZ);

						// Only needed for tile slice ranges
						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							hiZ,
							hiZSampler);
					})

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return ctx.frameState->activeLightCount > 0 && ctx.frameState->bHasVisibles;
					})

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::ClusteredLights,
							pass.passName);

						auto& pso = std::get<ComputeScope>(pass.scope);
						const auto& frameCtx = ctx.frameCtx;
						VkCommandBuffer cmd = ctx.commandBuffer;
						const auto& indirectArgs        = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::DispatchIndirectArgs);
						const auto& tileSliceRanges     = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::ClusterTileSliceRanges);
						const auto& clusterCounts       = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::ClusterCounts);
						const auto& clusterCursors      = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::ClusterCursors);
						const auto& clusterScanScratch  = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::ClusterScanScratch);
						const auto& clusterOffsets      = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::ClusterOffsets);

						// Buffers reset to zero
						pso.FillGpuBuffer(cmd, clusterCursors);
						pso.FillGpuBuffer(cmd, clusterCounts);
						pso.FillGpuBuffer(cmd, clusterScanScratch);


						// ==========================
						// Cluster Tile slice ranges
						// ==========================
						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_TILE_RANGES], pass.pushWriter);
						B::ComputeWriteToRead(cmd, tileSliceRanges);

						// =====================
						// Indirect arg compute
						// =====================
						//passScope.WriteSubPass(RD::Renderer_Pass::LightsIndirectDispatchArgs, "Indirect_Args");

						pso.SetIndirect(indirectArgs.m_buffer, RD::DISPATCH_LIGHTS_OFFSET_BYTES);
						pso.FillIndirectDispatch(cmd, RD::DISPATCH_SLOT_STRIDE_BYTES + RD::DISPATCH_SLOT_STRIDE_BYTES);
						B::CmdFillToComputeRW(cmd, indirectArgs);
						pso.ClearIndirect();

						pso.UpdateWorkgroups(WORKGROUP_1);
						pso.UpdateExtent({1, 1});

						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_INDIRECT_ARGS], pass.pushWriter);
						B::ComputeWriteToIndirectDispatchRead(cmd, indirectArgs);

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
						B::ComputeWriteToFragmentRead(cmd, clusterScanScratch);
					});
		});
}
