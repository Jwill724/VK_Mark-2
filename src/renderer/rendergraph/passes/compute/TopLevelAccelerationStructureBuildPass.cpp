#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../../backend/BufferBarriers.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessBDATable.h"
#include "../../../../profiler/Profiler.h"
#include "../../../frame/FrameContext.h"

namespace B = BufferBarriers;

static constexpr size_t PIPE_ID_BUILD = 0;

void RegisterTLASBuildPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"TLAS_Build",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.RunOnAsyncCompute()

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameCtx->IsTlasDirty() &&
							ctx.frameState->GetRTInstanceCount() > 0;
					})

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::TlasBuild,
							pass.passName,
							ctx.threadSlot,
							ctx.scheduleInfo->queue);

						const auto& frameCtx = ctx.frameCtx;
						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& rtInstances = ctx.frameCtx->GetGPUBuffer(RD::Renderer_Buffer::RTInstances);
						const uint32_t rtCount = ctx.frameState->GetRTInstanceCount();
						const uint32_t instanceCount = ctx.frameState->GetInstanceCount();

						pass.scope = ComputeScope{ { instanceCount, 1u }, { WORKGROUP_64 } };
						auto& pso = std::get<ComputeScope>(pass.scope);

						B::TLASInstanceReuseToCmdFill(cmd, rtInstances);
						pso.FillGpuBuffer(cmd, rtInstances);
						B::CmdFillToComputeAS(cmd, rtInstances);

						TlasPush push{};
						push.instanceCount = instanceCount;

						pso.SetPush(push);
						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_BUILD], pass.pushWriter);

						B::ComputeWriteToASBuildRead(cmd, rtInstances);

						VkAccelerationStructureGeometryKHR geom{
							VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
						geom.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
						geom.flags        = 0;

						auto& inst = geom.geometry.instances;
						inst.sType              = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
						inst.pNext              = nullptr;
						inst.arrayOfPointers    = VK_FALSE;
						inst.data.deviceAddress = rtInstances.m_address;

						VkAccelerationStructureBuildGeometryInfoKHR build{
							VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
						build.type                      = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
						build.flags                     = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
						build.mode                      = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
						build.geometryCount             = 1;
						build.pGeometries               = &geom;
						build.dstAccelerationStructure  = frameCtx->GetTLAS();
						build.scratchData.deviceAddress = frameCtx->GetTlasScratchAddress();

						VkAccelerationStructureBuildRangeInfoKHR range{};
						range.primitiveCount = rtCount;

						const VkAccelerationStructureBuildRangeInfoKHR* pRange = &range;
						vkCmdBuildAccelerationStructuresKHR(cmd, 1, &build, &pRange);

						B::ASBuildToRayQueryRead(cmd);

						frameCtx->ClearTlasFlag();
					});
		});
}
