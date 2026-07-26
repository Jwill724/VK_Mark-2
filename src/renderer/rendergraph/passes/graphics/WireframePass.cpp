#include "pch.h"

#include "../../RenderPasses.h"
#include "../../scopes/GraphicsScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../frame/FrameContext.h"

static constexpr size_t PIPE_ID_WIREFRAME = 0;

void RegisterWireframePass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Wireframe",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::Prepass)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->InstancesActive() &&
							ctx.frameState->IsWireframeOn();
					})

				.WriteResource(
					RD::Renderer_RenderTarget::Opaque,
					RD::ImageAccess::GraphicsColorWrite,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::DepthRaw,
					RD::ImageAccess::GraphicsDepthWrite,
					RD::ImageAccess::GraphicsDepthWrite)

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						pass.scope = GraphicsScope{};
						auto& pso = std::get<GraphicsScope>(pass.scope);
						const auto& frameCtx = ctx.frameCtx;

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto indirectBuffer =
							frameCtx->GetGPUBuffer(RD::Renderer_Buffer::IndirectDraws).m_buffer;

						const auto indirectCountBuffer =
							frameCtx->GetGPUBuffer(RD::Renderer_Buffer::IndirectDrawCounts).m_buffer;

						const auto& depthRaw = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthRaw);
						const auto& opaque = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Opaque);

						AttachmentDesc opaqueAttach{};
						opaqueAttach.imageView = opaque.m_imageView;
						opaqueAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

						opaqueAttach.loadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
						opaqueAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

						AttachmentDesc depthAttach{};
						depthAttach.imageView = depthRaw.m_imageView;
						depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
						depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
						depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
						depthAttach.SetDepth(0);

						pso.UpdateRenderInfo({ opaque.Width(), opaque.Height() }, { opaqueAttach, depthAttach });

						pso.BeginRendering(cmd);

						pso.DrawIndexedIndirectCount(
							cmd,
							RD::VIS_SLOT_OPAQUE,
							indirectBuffer,
							indirectCountBuffer,
							pass.pipelines[PIPE_ID_WIREFRAME],
							pass.pushWriter);

						pso.EndRendering(cmd);
					});
		});
}
