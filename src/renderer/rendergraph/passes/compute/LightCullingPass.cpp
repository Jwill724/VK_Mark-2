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

static constexpr size_t PIPE_ID_LIGHT_CULL = 0;

void RegisterLightCullingPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Light_Culling",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetSetup(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						pass.scope = ComputeScope{{ ctx.frameState->GetLightCount(), 1u }, { WORKGROUP_256 }};
						auto& pso = std::get<ComputeScope>(pass.scope);
					})

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
							return
								ctx.frameState->InstancesActive() &&
								ctx.frameState->LightsActive() &&
								!ctx.frameState->DebugRenderFastPath();
					})

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						VkCommandBuffer cmd = ctx.commandBuffer;
						const auto& frameCtx = ctx.frameCtx;
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							cmd,
							RD::Renderer_Pass::LightCulling,
							pass.passName);

						const auto& lightCountBuf = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::VisibleLightCount);
						const auto& visibleLightIdsBuf = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::VisibleLightIDs);

						auto& pso = std::get<ComputeScope>(pass.scope);

						pso.FillGpuBuffer(cmd, lightCountBuf);
						B::CmdFillToComputeRW(cmd, lightCountBuf);

						pso.DispatchComputePass(
							cmd,
							pass.pipelines[PIPE_ID_LIGHT_CULL],
							pass.pushWriter);

						B::ComputeWriteToRead(cmd, lightCountBuf);
						B::ComputeWriteToRead(cmd, visibleLightIdsBuf);
					});
		});
}
