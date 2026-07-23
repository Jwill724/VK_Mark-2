#include "pch.h"

#include "../../RenderPasses.h"
#include "../../scopes/GraphicsScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../backend/memory/BindlessBDATable.h"
#include "../../../../profiler/Profiler.h"
#include "../../../frame/FrameContext.h"

static constexpr size_t PIPE_ID_MAIN = 0;

void RegisterLineDebugPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Debug_Line",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->InstancesActive() &&
							ctx.frameState->IsObbLineOn() &&
							!ctx.frameState->DebugRenderFastPath();
					})

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

						AttachmentDesc opaqueAttach{};
						opaqueAttach.imageView = opaque.m_imageView;
						opaqueAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
						const bool firstWrite = ctx.renderGraph->IsFirstGraphicsWrite(RD::Renderer_RenderTarget::Opaque);
						opaqueAttach.loadOp  = firstWrite ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
						opaqueAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

						AttachmentDesc depthAttach{};
						depthAttach.imageView = depthResolved.m_imageView;
						depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
						depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
						depthAttach.SetDepth(0.0f);

						pso.UpdateRenderInfo(
							{
								opaque.Width(),
								opaque.Height()
							},
							{ opaqueAttach, depthAttach });
					})

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::DebugLineDraw,
							pass.passName);

						auto& pso = std::get<GraphicsScope>(pass.scope);
						const auto& frameCtx = ctx.frameCtx;

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& debugDrawBuffer = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::DebugDraw);
						const auto& debugVertexBuffer = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::DebugVertex);

						pso.BeginRendering(cmd);

						pso.DrawIndirect(
							cmd,
							debugDrawBuffer.m_buffer,
							debugVertexBuffer.m_buffer,
							0u, // Offsets
							0u,
							pass.pipelines[PIPE_ID_MAIN]);

						pso.EndRendering(cmd);
					});
		});
}
