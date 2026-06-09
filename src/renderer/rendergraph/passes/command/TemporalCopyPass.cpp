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
				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return ctx.frameState->bIsOpaqueVisible && ctx.frameState->bTemporalValid;
					})

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& prevDepthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PrevDepthResolved);
						const auto& velocity = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Velocity);
						const auto& prevVelocity = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PrevVelocity);

						ImageUtils::ImageCopy(
							cmd,
							depthResolved,
							prevDepthResolved,
							RD::ImageAccess::DepthRead,
							RD::ImageAccess::DepthRead,
							RD::ImageAccess::GraphicsDepthWrite,
							RD::ImageAccess::DepthRead);

						ImageUtils::ImageCopy(
							cmd,
							velocity,
							prevVelocity,
							RD::ImageAccess::Read,
							RD::ImageAccess::Read,
							RD::ImageAccess::GraphicsColorWrite,
							RD::ImageAccess::Read);
					});
		});
}
