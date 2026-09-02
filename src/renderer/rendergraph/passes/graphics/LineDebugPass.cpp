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
				.SetPhase(RenderPhase::Lighting)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->InstancesActive() &&
							ctx.frameState->IsObbLineOn() &&
							!ctx.frameState->DebugRenderFastPath();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)

				.WriteResource(
					RD::Renderer_RenderTarget::HDRScene,
					RD::ImageAccess::GraphicsColorWrite,
					RD::ImageAccess::ComputeWrite)

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::DebugLineDraw,
							pass.passName);

						pass.scope = GraphicsScope{};
						auto& pso = std::get<GraphicsScope>(pass.scope);

						const auto& frameCtx = ctx.frameCtx;

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& hdrScene = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HDRScene);

						const auto& debugDrawBuffer = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::DebugDraw);
						const auto& debugVertexBuffer = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::DebugVertex);

						AttachmentDesc opaqueAttach{};
						opaqueAttach.imageView = hdrScene.m_imageView;
						opaqueAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
						opaqueAttach.loadOp  = VK_ATTACHMENT_LOAD_OP_LOAD;
						opaqueAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

						AttachmentDesc depthAttach{};
						depthAttach.imageView = depthResolved.m_imageView;
						depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
						depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
						depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_NONE;
						depthAttach.SetDepth(0);

						pso.UpdateRenderInfo(
							{
								hdrScene.Width(),
								hdrScene.Height()
							},
							{ opaqueAttach, depthAttach });

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
