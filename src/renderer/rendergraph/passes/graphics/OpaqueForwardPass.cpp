#include "pch.h"

#include "../../RenderPasses.h"
#include "../../scopes/GraphicsScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"
#include "../../../frame/FrameContext.h"

static constexpr size_t PIPE_ID_MAIN = 0;

void RegisterOpaqueForwardPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Opaque_Forward",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.ReadResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)

				.ReadResource(
					RD::Renderer_RenderTarget::AORaw,
					RD::ImageAccess::Read)
				.ReadResource(
					RD::Renderer_RenderTarget::SSContactShadows,
					RD::ImageAccess::Read)
				.ReadResource(
					RD::Renderer_RenderTarget::BentNormals,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::Opaque,
					RD::ImageAccess::GraphicsColorWrite,
					RD::ImageAccess::Read)

				.SetSetup(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						pass.scope = GraphicsScope{};
						auto& pso = std::get<GraphicsScope>(pass.scope);

						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& opaque = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Opaque);
						const auto& aoRaw = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::AORaw);
						const auto& bentNormals = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::BentNormals);
						const auto& contactShadows = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::SSContactShadows);
						const auto nearestClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);
						const auto linearClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::LinearClamp);

						AttachmentDesc opaqueAttach{};
						opaqueAttach.imageView = opaque.m_imageView;
						opaqueAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

						AttachmentDesc depthAttach{};
						depthAttach.imageView = depthResolved.m_imageView;
						depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
						depthAttach.clearValue.depthStencil.depth = 0.0f;

						pso.UpdateRenderInfo({ opaque.Width(), opaque.Height() }, { opaqueAttach, depthAttach });

						pso.SetPush(ctx.profiler->forwardPush);

						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, aoRaw, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, contactShadows, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_3, bentNormals, linearClampSampler);
					})

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return ctx.frameState->bIsOpaqueVisible;
					})

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::OpaqueForward,
							pass.passName);

						auto& pso = std::get<GraphicsScope>(pass.scope);
						const auto& frameCtx = ctx.frameCtx;

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& indirectBuffer =
							frameCtx->GetGPUBuffer(RD::Renderer_Buffer::IndirectDraws).m_buffer;

						pso.BeginRendering(cmd);

						pso.DrawIndexedIndirect(
							cmd,
							indirectBuffer,
							frameCtx->GetOpaqueDrawRange(),
							pass.pipelines[PIPE_ID_MAIN],
							pass.pushWriter);

						pso.EndRendering(cmd);
					});
		});
}
