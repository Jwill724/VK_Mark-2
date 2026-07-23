#include "pch.h"

#include "../../RenderPasses.h"
#include "../../scopes/GraphicsScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../backend/memory/BindlessBDATable.h"
#include "../../../../profiler/Profiler.h"
#include "../../../frame/FrameContext.h"
#include "../../../scene/Scene.h"

static constexpr size_t PIPE_ID_MAIN = 0;

void RegisterFlashlightShadowMapPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Flashlight_Shadow_Map",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->IsShadowsOn() &&
							ctx.frameState->IsFlashlightOn() &&
							ctx.frameState->InstancesActive() &&
							!ctx.frameState->DebugRenderFastPath();
					})

				.WriteResource(
					RD::Renderer_RenderTarget::FlashlightShadowMap,
					RD::ImageAccess::GraphicsDepthWrite,
					RD::ImageAccess::DepthRead)

				.SetSetup(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						pass.scope = GraphicsScope{};
						auto& pso = std::get<GraphicsScope>(pass.scope);

						const auto& shadowmap = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::FlashlightShadowMap);

						AttachmentDesc depth{};
						depth.imageView = shadowmap.m_imageView;
						depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
						depth.SetDepth(1.0f);

						pso.SetPush(ctx.scene->GetSceneData().flashlightVP);

						pso.UpdateRenderInfo(
							{
								shadowmap.Width(),
								shadowmap.Height()
							},
							{ depth });
					})

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::FlashlightShadow,
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
							RD::VIS_SLOT_FLASHLIGHT,
							indirectBuffer,
							indirectCountBuffer,
							pipeline,
							pass.pushWriter);

						pso.EndRendering(cmd);
					});
		});
}
