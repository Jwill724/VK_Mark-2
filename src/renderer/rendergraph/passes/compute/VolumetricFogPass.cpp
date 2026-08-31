#include "pch.h"

#include "../../RenderPasses.h"
//#include "../../../rendergraph/RenderGraphBuilder.h"
//#include "../../scopes/ComputeScope.h"
//#include "../../../backend/BufferBarriers.h"
//#include "EngineTypes.h"
//#include "../../RenderGraph.h"
//#include "../../RenderGraphResources.h"
//#include "../../../backend/memory/BindlessImageTable.h"
//#include "../../../backend/memory/BindlessBDATable.h"
//#include "../../../../profiler/Profiler.h"
//#include "../../../frame/FrameContext.h"
//
//namespace B = BufferBarriers;
//
//static constexpr size_t PIPE_ID_INJECT    = 0;
//static constexpr size_t PIPE_ID_REPROJECT = 1;
//static constexpr size_t PIPE_ID_INTEGRATE = 2;
//
//void RegisterVolumetricFogPass(
//	RenderGraph& graph,
//	const std::vector<PipelineHandle> pipelines)
//{
//	graph.AddPass(
//		"Volumetric_Fog",
//		pipelines,
//		[&](RenderPassBuilder& builder)
//		{
//			builder
//				.SetPhase(RenderPhase::AsyncWindow)
//
//				.SetExecutionCondition(
//					[](const RenderPassExecutionContext& ctx)
//					{
//						return
//							ctx.frameState->InstancesActive() &&
//							ctx.frameState->IsVolumetricsOn() &&
//							!ctx.frameState->DebugRenderFastPath();
//					})
//
//				.ReadResource(
//					RD::Renderer_RenderTarget::HiZ,
//					RD::ImageAccess::Read,
//					0u, VK_REMAINING_MIP_LEVELS)
//
//				.SetRecord(
//					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
//					{
//						auto passScope = ctx.profiler->ProfilePass(
//							*ctx.frameCtx,
//							ctx.commandBuffer,
//							RD::Renderer_Pass::VolumetricFog,
//							pass.passName,
//							ctx.threadSlot,
//							ctx.scheduleInfo->queue);
//
//						pass.scope = ComputeScope{ { 1u, 1u }, { WORKGROUP_256 } };
//						auto& pso = std::get<ComputeScope>(pass.scope);
//
//						const auto& frameCtx = ctx.frameCtx;
//						VkCommandBuffer cmd = ctx.commandBuffer;
//
//						const auto& indirectArgs = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::DispatchIndirectArgs);
//						const auto& tileSliceRanges = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::ClusterTileSliceRanges);
//						const auto& clusterCounts = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::ClusterCounts);
//						const auto& clusterCursors = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::ClusterCursors);
//						const auto& clusterScanScratch = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::ClusterScanScratch);
//						const auto& clusterOffsets = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::ClusterOffsets);
//
//						const auto& lightCountBuf = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::VisibleLightCount);
//						const auto& visibleLightIdsBuf = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::VisibleLightIDs);
//						const auto& transparentClusterBounds = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::ClusterTileTransparentNear);
//
//						const auto& hiZ = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HiZ);
//						const auto hiZSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::HiZ);
//
//					});
//		});
//}
