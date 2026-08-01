#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../backend/ImageUtils.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"

void RegisterTemporalCopyPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Temporal_Copy",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::Visibility)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->IsTaaOn() &&
							ctx.frameState->InstancesActive() &&
							ctx.frameState->IsTemporalValid() &&
							!ctx.frameState->DebugRendering();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::TransferSrc)
 
				.ReadResource(
					RD::Renderer_RenderTarget::Velocity,
					RD::ImageAccess::TransferSrc)
 
				.WriteResource(
					RD::Renderer_RenderTarget::PrevDepthResolved,
					RD::ImageAccess::TransferDst,
					RD::ImageAccess::DepthRead)
 
				.WriteResource(
					RD::Renderer_RenderTarget::PrevVelocity,
					RD::ImageAccess::TransferDst,
					RD::ImageAccess::Read)

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& prevDepthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PrevDepthResolved);
						const auto& velocity = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Velocity);
						const auto& prevVelocity = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PrevVelocity);

						ImageUtils::ImageCopyNoBarrier(cmd, depthResolved, prevDepthResolved);
						ImageUtils::ImageCopyNoBarrier(cmd, velocity,      prevVelocity);
					});
		});
}
