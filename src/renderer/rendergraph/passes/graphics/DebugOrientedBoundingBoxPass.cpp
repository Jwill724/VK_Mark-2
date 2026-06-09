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

static struct alignas(16) OBBPush
{
	VkDeviceAddress vertexBuffer;
	uint32_t pad0[2];
};

void RegisterOBBLineDebugPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"OBB_Line_Debug",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
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
						depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
						depthAttach.clearValue.depthStencil.depth = 0.0f;

						pso.UpdateRenderInfo(
							{
								opaque.Width(),
								opaque.Height()
							},
							{ opaqueAttach, depthAttach });
					})

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return ctx.frameState->bHasVisibles &&
							ctx.profiler->debugToggles.enableOBBs;
					})

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::OBBLineView,
							pass.passName);

						auto& pso = std::get<GraphicsScope>(pass.scope);
						const auto& frameCtx = ctx.frameCtx;

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& obbBuffer   = frameCtx->GetOBBLineDebugBuffer();
						const auto& drawOffsets = frameCtx->GetOBBDrawOffsets();

						// line topology -> TrianglesFromNonIndexed would return 0 anyway
						ctx.profiler->AddDirect(
							static_cast<uint32_t>(drawOffsets.size()),
							0u);

						OBBPush obbPush{ .vertexBuffer = obbBuffer.m_address };
						pso.SetPush(obbPush);

						pso.BeginRendering(cmd);

						pso.DrawVertexPull(
							cmd,
							obbBuffer.m_buffer,
							0,
							drawOffsets,
							pass.pipelines[PIPE_ID_MAIN]);

						pso.EndRendering(cmd);
					});
		});
}
