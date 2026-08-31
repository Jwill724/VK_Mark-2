#include "pch.h"

#include "../../RenderPasses.h"
#include "../../scopes/GraphicsScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../frame/FrameContext.h"

static constexpr size_t PIPE_ID_WIREFRAME      = 0;

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

				.ReadResource(
					RD::Renderer_RenderTarget::HiZ,
					RD::ImageAccess::MeshShaderRead)

				.WriteResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::GraphicsDepthWrite,
					RD::ImageAccess::DepthRead)

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						pass.scope = GraphicsScope{};
						auto& pso = std::get<GraphicsScope>(pass.scope);
						const auto& frameCtx = ctx.frameCtx;

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto indirectCountBuffer =
							frameCtx->GetGPUBuffer(RD::Renderer_Buffer::IndirectDrawCounts).m_buffer;

						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& opaque = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Opaque);

						AttachmentDesc opaqueAttach{};
						opaqueAttach.imageView = opaque.m_imageView;
						opaqueAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
						opaqueAttach.SetColor({ 0.0f, 0.0f, 0.0f, 1.0f });

						AttachmentDesc depthAttach{};
						depthAttach.imageView = depthResolved.m_imageView;
						depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
						depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
						depthAttach.SetDepth(0);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HiZ),
							ctx.imageTable->GetSampler(RD::Renderer_Sampler::HiZ),
							VK_REMAINING_MIP_LEVELS,
							RD::ImageAccess::MeshShaderRead);

						PrepassTaskPush push{};
						push.slot = RD::VIS_SLOT_OPAQUE;
						push.phase = 2u;
						pso.SetPush(push);

						pso.UpdateRenderInfo(
							{ opaque.Width(), opaque.Height() },
							{ opaqueAttach, depthAttach });

						pso.BeginRendering(cmd);

						pso.DrawMeshTasksIndirectCount(
							cmd,
							RD::VIS_SLOT_OPAQUE,
							frameCtx->GetGPUBuffer(RD::Renderer_Buffer::TaskDispatch).m_buffer,
							frameCtx->GetGPUBuffer(RD::Renderer_Buffer::IndirectDrawCounts).m_buffer,
							pass.pipelines[PIPE_ID_WIREFRAME],
							pass.pushWriter);

						pso.EndRendering(cmd);
					});
		});
}
