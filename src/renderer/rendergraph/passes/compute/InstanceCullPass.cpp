#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../../backend/BufferBarriers.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../../profiler/Profiler.h"
#include "../../../frame/FrameContext.h"
#include "../../../backend/memory/BindlessImageTable.h"

namespace B = BufferBarriers;

static constexpr size_t PIPE_ID_INSTANCE_CULL = 0;

void RegisterInstanceCullPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Instance_Cull",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetSetup(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						pass.scope = ComputeScope{{ ctx.frameState->activeInstanceCount, 1u }, WORKGROUP_256 };
						auto& pso = std::get<ComputeScope>(pass.scope);

						const auto& hiz = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HiZ);
						const auto hizSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::HiZ);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							hiz,
							hizSampler);
					})

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return ctx.frameState->activeInstanceCount > 0;
					})
				.DisableCulling()

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						VkCommandBuffer cmd = ctx.commandBuffer;
						const auto& frameCtx = ctx.frameCtx;
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							cmd,
							RD::Renderer_Pass::InstanceCull,
							pass.passName);

						auto& pso = std::get<ComputeScope>(pass.scope);

						const auto& bdaTable = frameCtx->GetBindlessBDATable();
						const auto& instanceVisibilityBuffer = bdaTable.GetGPUBuffer(RD::Renderer_Buffer::InstanceVisibility);
						const auto& visibleCountBuffer = bdaTable.GetGPUBuffer(RD::Renderer_Buffer::VisibleCount);
						const auto& drawBinCountersBuffer = bdaTable.GetGPUBuffer(RD::Renderer_Buffer::DrawBinCounters);
						const auto& indirectDrawCountsBuffer = bdaTable.GetGPUBuffer(RD::Renderer_Buffer::IndirectDrawCounts);
						const auto& visibleInstancesBuffer = bdaTable.GetGPUBuffer(RD::Renderer_Buffer::VisibleInstances);

						pso.FillGpuBuffer(cmd, instanceVisibilityBuffer);
						pso.FillGpuBuffer(cmd, visibleCountBuffer);
						pso.FillGpuBuffer(cmd, drawBinCountersBuffer);
						pso.FillGpuBuffer(cmd, indirectDrawCountsBuffer);

						B::CmdFillToComputeRW(cmd, instanceVisibilityBuffer);
						B::CmdFillToComputeRW(cmd, visibleCountBuffer);
						B::CmdFillToComputeRW(cmd, drawBinCountersBuffer);
						B::CmdFillToComputeRW(cmd, indirectDrawCountsBuffer);

						if (ctx.profiler->debugToggles.enableProfilerView)
						{
							const auto& gpuStatsBuffer = bdaTable.GetGPUBuffer(RD::Renderer_Buffer::DrawStats);
							pso.FillGpuBuffer(cmd, gpuStatsBuffer);
							B::CmdFillToComputeRW(cmd, gpuStatsBuffer);
						}

						pso.DispatchComputePass(
							cmd,
							pass.pipelines[PIPE_ID_INSTANCE_CULL],
							pass.pushWriter);

						B::ComputeWriteToRead(cmd, instanceVisibilityBuffer);
						B::ComputeWriteToRead(cmd, visibleInstancesBuffer);
						B::ComputeWriteToRead(cmd, visibleCountBuffer);
					});
		});
}
