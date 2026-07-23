#include "pch.h"

#include "../../RenderPasses.h"
#include "../../scopes/GraphicsScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"
#include "../../../frame/FrameContext.h"

static constexpr size_t PIPE_ID_MAIN = 0;

void RegisterThePrepass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Depth_Prepass",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return ctx.frameState->InstancesActive();
					})
				.DisableCulling()

				.WriteResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::GraphicsDepthWrite,
					RD::ImageAccess::DepthRead)

				.WriteResource(
					RD::Renderer_RenderTarget::Visibility,
					RD::ImageAccess::GraphicsColorWrite,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::ViewSpaceNormals,
					RD::ImageAccess::GraphicsColorWrite,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::Velocity,
					RD::ImageAccess::GraphicsColorWrite,
					RD::ImageAccess::Read)

				.SetSetup(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						pass.scope = GraphicsScope{};
						auto& pso = std::get<GraphicsScope>(pass.scope);

						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& visibility = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Visibility);
						const auto& viewSpaceNormals = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::ViewSpaceNormals);
						const auto& velocity = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Velocity);

						AttachmentDesc prepassDepth{};
						prepassDepth.imageView = depthResolved.m_imageView;
						prepassDepth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
						prepassDepth.SetDepth(0.0f);

						AttachmentDesc prepassVisibility{};
						prepassVisibility.imageView = visibility.m_imageView;
						prepassVisibility.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
						uint32_t resetIndices[4] = { RD::INVALID_U32, RD::INVALID_U32, 0u, 0u };
						prepassVisibility.SetColorU32(resetIndices);

						AttachmentDesc prepassNormal{};
						prepassNormal.imageView = viewSpaceNormals.m_imageView;
						prepassNormal.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
						prepassNormal.SetColor({0.5f, 0.5f, 1.0f, 1.0f});

						AttachmentDesc prepassVelocity{};
						prepassVelocity.imageView = velocity.m_imageView;
						prepassVelocity.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
						prepassVelocity.SetColor({0.0f});

						pso.UpdateRenderInfo(
							{
								depthResolved.Width(),
								depthResolved.Height()
							},
							{ prepassVisibility, prepassNormal, prepassVelocity, prepassDepth });
					})

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::Prepass,
							pass.passName);

						auto& pso = std::get<GraphicsScope>(pass.scope);
						const auto& frameCtx = ctx.frameCtx;

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto indirectBuffer =
							frameCtx->GetGPUBuffer(RD::Renderer_Buffer::IndirectDraws).m_buffer;

						const auto indirectCountBuffer =
							frameCtx->GetGPUBuffer(RD::Renderer_Buffer::IndirectDrawCounts).m_buffer;

						pso.BeginRendering(cmd);

						pso.DrawIndexedIndirectCount(
							cmd,
							RD::VIS_SLOT_OPAQUE,
							indirectBuffer,
							indirectCountBuffer,
							pass.pipelines[PIPE_ID_MAIN],
							pass.pushWriter);

						pso.EndRendering(cmd);
					});
		});
}
