#include "pch.h"

#include "../../RenderPasses.h"
#include "../../scopes/GraphicsScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"
#include "../../../frame/FrameContext.h"

static constexpr size_t PIPE_ID_MAIN = 0;

void RegisterTransparentForwardPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Transparent_Forward",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return ctx.frameState->InstancesActive();
					})

				.WriteResource(
					RD::Renderer_RenderTarget::TransparentAccumulation,
					RD::ImageAccess::GraphicsColorWrite,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::TransparentRevealage,
					RD::ImageAccess::GraphicsColorWrite,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::DepthRaw,
					RD::ImageAccess::GraphicsDepthWrite,
					RD::ImageAccess::GraphicsDepthWrite)

				.SetSetup(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						pass.scope = GraphicsScope{};
						auto& pso = std::get<GraphicsScope>(pass.scope);

						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& transparentAccum = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::TransparentAccumulation);
						const auto& transparentReveal = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::TransparentRevealage);

						AttachmentDesc tAccumAttach{};
						tAccumAttach.imageView = transparentAccum.m_imageView;
						tAccumAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
						tAccumAttach.SetColor({0.0f});

						AttachmentDesc tRevealAttach{};
						tRevealAttach.imageView = transparentReveal.m_imageView;
						tRevealAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
						tRevealAttach.SetColor({ 1.0f, 0.0f, 0.0f, 0.0f });

						AttachmentDesc depthAttach{};
						depthAttach.imageView = depthResolved.m_imageView;
						depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
						depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
						depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
						depthAttach.SetDepth(0.0f);

						if (!ctx.frameState->InstancesActive())
						{
							const auto& depthRaw = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthRaw);
							depthAttach.imageView = depthRaw.m_imageView;
							depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
							depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
							depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
						}

						pso.UpdateRenderInfo({ depthResolved.Width(), depthResolved.Height() }, // All images are the same size as renderer drawExtent
							{ tAccumAttach, tRevealAttach, depthAttach });

						pso.SetPush(ctx.profiler->forwardPush);
					})

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::TransparentForward,
							pass.passName);

						auto& pso = std::get<GraphicsScope>(pass.scope);
						const auto& frameCtx = ctx.frameCtx;

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto indirectBuffer =
							frameCtx->GetGPUBuffer(RD::Renderer_Buffer::IndirectDraws).m_buffer;

						const auto indirectCountBuffer =
							frameCtx->GetGPUBuffer(RD::Renderer_Buffer::IndirectDrawCounts).m_buffer;

						const auto& pipeline  = pass.pipelines[PIPE_ID_MAIN];

						pso.BeginRendering(cmd);

						pso.DrawIndexedIndirectCount(
							cmd,
							RD::VIS_SLOT_TRANSPARENT,
							indirectBuffer,
							indirectCountBuffer,
							pipeline,
							pass.pushWriter);

						pso.EndRendering(cmd);
					});
		});
}
