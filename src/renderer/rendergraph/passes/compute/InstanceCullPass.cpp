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

void RegisterInstanceCullPass(RenderGraph& graph)
{
	graph.AddPass(
		"Instance_Cull",
		{ RP::InstanceCull },
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
							RD::Renderer_Pass::InstanceCull,
							pass.passName);

						pass.scope = ComputeScope{{ ctx.frameState->GetInstanceCount(), 1u }, WORKGROUP_256 };
						auto& pso = std::get<ComputeScope>(pass.scope);

						const auto& bdaTable = frameCtx->GetBindlessBDATable();
						const auto& instanceVisibilityBuffer = bdaTable.GetGPUBuffer(RD::Renderer_Buffer::InstanceVisibility);
						const auto& visibleCountBuffer = bdaTable.GetGPUBuffer(RD::Renderer_Buffer::VisibleCount);
						const auto& drawBinCountersBuffer = bdaTable.GetGPUBuffer(RD::Renderer_Buffer::DrawBinCounters);
						const auto& indirectDrawCountsBuffer = bdaTable.GetGPUBuffer(RD::Renderer_Buffer::IndirectDrawCounts);
						const auto& visibleInstancesBuffer = bdaTable.GetGPUBuffer(RD::Renderer_Buffer::VisibleInstances);

						const auto& visB = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::MeshletVisibilityB);
						pso.FillGpuBuffer(cmd, visB, 0u, 0, VK_WHOLE_SIZE);
						BufferBarriers::CmdFillToMeshRW(cmd, visB);

						const auto& rtRayList = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::RTRayList);
						pso.FillGpuBuffer(cmd, rtRayList, 0u, 0, RTRayListLayout::HEADER_BYTES);
						B::CmdFillToComputeRW(cmd, rtRayList);

						if (!frameCtx->IsMeshletVisibilityBufferInitialized())
						{
							const auto& visA = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::MeshletVisibilityA);
							pso.FillGpuBuffer(cmd, visA, 0u, 0, VK_WHOLE_SIZE);
							BufferBarriers::CmdFillToMeshRW(cmd, visA);
							frameCtx->MeshletVisibilityBufferValid();
						}

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
							ctx.Pipe(RP::InstanceCull),
							pass.pushWriter);

						B::ComputeWriteToRead(cmd, instanceVisibilityBuffer);
						B::ComputeWriteToRead(cmd, visibleInstancesBuffer);
						B::ComputeWriteToRead(cmd, visibleCountBuffer);
					});
		});
}
