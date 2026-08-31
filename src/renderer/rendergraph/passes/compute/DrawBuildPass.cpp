#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../../backend/BufferBarriers.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../../profiler/Profiler.h"
#include "../../../frame/FrameContext.h"

namespace B = BufferBarriers;

static constexpr size_t PIPE_ID_DRAW_ARGS    = 0;
static constexpr size_t PIPE_ID_DRAW_SCATTER = 1;
static constexpr size_t PIPE_ID_DRAW_EMIT    = 2;
static constexpr size_t PIPE_ID_DRAW_PLACE   = 3;

void RegisterDrawBuildPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Draw_Build",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::Visibility)
				.ForceExecution()

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						VkCommandBuffer cmd = ctx.commandBuffer;
						const auto& frameCtx = ctx.frameCtx;
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							cmd,
							RD::Renderer_Pass::DrawBuild,
							pass.passName);

						pass.scope = ComputeScope{{ 1u, 1u }, WORKGROUP_NONE };
						auto& pso = std::get<ComputeScope>(pass.scope);
						pso.UpdateWorkgroups(WORKGROUP_1, true);

						const auto& bdaTable                   = frameCtx->GetBindlessBDATable();
						const auto& drawBinsBuffer             = bdaTable.GetGPUBuffer(RD::Renderer_Buffer::DrawBins);
						const auto& drawBinCountersBuffer      = bdaTable.GetGPUBuffer(RD::Renderer_Buffer::DrawBinCounters);
						const auto& indirectDrawCountsBuffer   = bdaTable.GetGPUBuffer(RD::Renderer_Buffer::IndirectDrawCounts);
						const auto& drawInstanceIDsBuffer      = bdaTable.GetGPUBuffer(RD::Renderer_Buffer::DrawInstanceIDs);
						const auto& instanceCursorsBuffer      = bdaTable.GetGPUBuffer(RD::Renderer_Buffer::InstanceCursors);
						const auto& dispatchIndirectArgsBuffer = bdaTable.GetGPUBuffer(RD::Renderer_Buffer::DispatchIndirectArgs);
						const auto& instanceStreamsBuffer      = bdaTable.GetGPUBuffer(RD::Renderer_Buffer::InstanceStreams);

						// =================
						// === DRAW ARGS ===
						// =================

						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_DRAW_ARGS], pass.pushWriter);

						B::ComputeWriteToIndirectRead(cmd, dispatchIndirectArgsBuffer);
						B::ComputeWriteToRW(cmd, instanceCursorsBuffer);

						// ====================
						// === DRAW SCATTER ===
						// ====================

						pso.SetIndirect(dispatchIndirectArgsBuffer.m_buffer, RD::DISPATCH_SCATTER_OFFSET_BYTES);
						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_DRAW_SCATTER], pass.pushWriter);
						pso.ClearIndirect();

						B::ComputeWriteToRW(cmd, drawBinCountersBuffer);
						B::ComputeWriteToRead(cmd, instanceStreamsBuffer);
						B::ComputeWriteToRead(cmd, instanceCursorsBuffer);

						// =================
						// === DRAW EMIT ===
						// =================

						pso.UpdateWorkgroups({ 1u, RD::VIS_SLOT_COUNT, 1u }, true);
						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_DRAW_EMIT], pass.pushWriter);

						B::ComputeWriteToRead(cmd, drawBinsBuffer);
						B::ComputeWriteToRW(cmd, drawBinCountersBuffer);
						B::ComputeWriteToIndirectRead(cmd, indirectDrawCountsBuffer);

						// ==================
						// === DRAW PLACE ===
						// ==================

						for (uint32_t s = 0u; s < RD::VIS_SLOT_COUNT; ++s)
						{
							BindlessAccessPush p;
							p.id0 = s;
							pso.SetPush(p);
							pso.SetIndirect(dispatchIndirectArgsBuffer.m_buffer,
								static_cast<size_t>(s) * RD::DISPATCH_SLOT_STRIDE_BYTES);
							pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_DRAW_PLACE], pass.pushWriter);
						}

						B::ComputeWriteToRW(cmd, drawBinCountersBuffer);

						B::ComputeWriteToTaskRead(cmd, drawInstanceIDsBuffer);
					});
		});
}
