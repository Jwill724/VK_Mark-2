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
							ctx.frameState->InstancesActive() &&
							ctx.frameState->IsTemporalValid();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::TransferSrc)

				.ReadResource(
					RD::Renderer_RenderTarget::Velocity,
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

				.WriteResource(
					RD::Renderer_RenderTarget::PrevVelocity,
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
						const auto& velocity = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Velocity);
						const auto& prevVelocity = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PrevVelocity);

						ImageUtils::ImageCopyNoBarrier(cmd, depthResolved,    prevDepthResolved);
						ImageUtils::ImageCopyNoBarrier(cmd, velocity,         prevVelocity);
						ImageUtils::ImageCopyNoBarrier(cmd, viewSpaceNormals, prevViewSpaceNormals);
					});
		});
}
