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

static constexpr size_t PIPE_ID_DEBUG_COUNT = 0;
static constexpr size_t PIPE_ID_DEBUG_ARGS = 1;
static constexpr size_t PIPE_ID_DEBUG_BUILD = 2;

static struct alignas(16) DebugCountPush
{
	uint32_t debugMask = UINT32_MAX;
	uint32_t streamFilter = UINT32_MAX;
	uint32_t pad0;
	uint32_t pad1;
};

void RegisterDebugDrawBuildPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Debug_Draw_Build",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return ctx.frameState->activeInstanceCount > 0 && ctx.frameState->bDebugLine;
					})

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						VkCommandBuffer cmd = ctx.commandBuffer;
						const auto& frameCtx = ctx.frameCtx;
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							cmd,
							RD::Renderer_Pass::DebugDrawBuild,
							pass.passName);

						const auto& dbg = ctx.profiler->debugToggles;

						pass.scope = ComputeScope{{ 1u, 1u }};
						auto& pso = std::get<ComputeScope>(pass.scope);

						const auto& bdaTable = frameCtx->GetBindlessBDATable();
						const auto& dispatchArgsBuffer = bdaTable.GetGPUBuffer(RD::Renderer_Buffer::DispatchIndirectArgs);
						const auto& debugCountsBuffer = bdaTable.GetGPUBuffer(RD::Renderer_Buffer::DebugCounts);
						const auto& debugItemsBuffer = bdaTable.GetGPUBuffer(RD::Renderer_Buffer::DebugItems);
						const auto& debugVertexBuffer = bdaTable.GetGPUBuffer(RD::Renderer_Buffer::DebugVertex);
						const auto& debugDrawBuffer = bdaTable.GetGPUBuffer(RD::Renderer_Buffer::DebugDraw);

						pso.FillGpuBuffer(cmd, debugCountsBuffer);
						B::CmdFillToComputeRW(cmd, debugCountsBuffer);

						// ===================
						// === DEBUG COUNT ===
						// ===================

						uint32_t streamFilter = 0u;

						if (dbg.showOpaqueOBBs)      streamFilter |= RD::VIS_PRIMARY_OPAQUE;
						if (dbg.showTransparentOBBs) streamFilter |= RD::VIS_PRIMARY_TRANSPARENT;

						DebugCountPush push;
						push.debugMask |= RD::DEBUG_MASK_OBB;
						push.streamFilter = streamFilter;

						pso.SetIndirect(dispatchArgsBuffer.m_buffer, RD::DISPATCH_SCATTER_OFFSET_BYTES);
						pso.SetPush(push);
						pso.DispatchComputePass(
							cmd,
							pass.pipelines[PIPE_ID_DEBUG_COUNT],
							pass.pushWriter);
						pso.ClearIndirect();
						pso.ClearPush();

						B::ComputeWriteToRW(cmd, debugCountsBuffer);
						B::ComputeWriteToRead(cmd, debugItemsBuffer);

						// ==================
						// === DEBUG ARGS ===
						// ==================

						pso.UpdateWorkgroups(WORKGROUP_1, true);
						pso.DispatchComputePass(
							cmd,
							pass.pipelines[PIPE_ID_DEBUG_ARGS],
							pass.pushWriter);

						B::ComputeWriteToIndirectRead(cmd, dispatchArgsBuffer);
						B::ComputeWriteToIndirectRead(cmd, debugDrawBuffer);

						// ===================
						// === DEBUG BUILD ===
						// ===================
						pso.SetIndirect(dispatchArgsBuffer.m_buffer, RD::DISPATCH_DEBUG_BUILD_OFFSET_BYTES);
						pso.DispatchComputePass(
							cmd,
							pass.pipelines[PIPE_ID_DEBUG_BUILD],
							pass.pushWriter);
						B::ComputeWriteToVertexRead(cmd, debugVertexBuffer);
					});
		});
}
