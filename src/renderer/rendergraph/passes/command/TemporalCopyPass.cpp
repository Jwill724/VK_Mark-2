#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../backend/ImageUtils.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"

void RegisterTemporalCopyPass(RenderGraph& graph)
{
	graph.AddPass(
		"Temporal_Copy",
		{},
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::Visibility)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->InstancesActive() &&
							ctx.frameState->IsTemporalValid();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::TransferSrc)

				.ReadResource(
					RD::Renderer_RenderTarget::ViewNormals,
					RD::ImageAccess::TransferSrc)

				.WriteResource(
					RD::Renderer_RenderTarget::PrevDepthResolved,
					RD::ImageAccess::TransferDst,
					RD::ImageAccess::DepthRead)

				.WriteResource(
					RD::Renderer_RenderTarget::PrevViewNormals,
					RD::ImageAccess::TransferDst,
					RD::ImageAccess::ComputeRead)

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& prevDepthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PrevDepthResolved);
						const auto& viewSpaceNormals = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::ViewNormals);
						const auto& prevViewSpaceNormals = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PrevViewNormals);

						ImageUtils::ImageCopyNoBarrier(cmd, depthResolved,    prevDepthResolved);
						ImageUtils::ImageCopyNoBarrier(cmd, viewSpaceNormals, prevViewSpaceNormals);
					});
		});
}
