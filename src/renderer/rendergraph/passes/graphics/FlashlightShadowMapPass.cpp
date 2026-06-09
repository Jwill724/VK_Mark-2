#include "pch.h"

#include "../../RenderPasses.h"
#include "../../scopes/GraphicsScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../backend/memory/BindlessBDATable.h"
#include "../../../../profiler/Profiler.h"
#include "../../../frame/FrameContext.h"
#include "../../../scene/LightingSystem.h"

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
						depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
						depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
						depth.clearValue.depthStencil.depth = 1.0f;

						pso.SetPush(LightingSystem::_mainFlashLight.ViewProj);

						pso.UpdateRenderInfo(
							{
								shadowmap.Width(),
								shadowmap.Height()
							},
							{ depth });
					})

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return ctx.profiler->debugToggles.enableShadows &&
							ctx.frameState->bFlashlightOn &&
							ctx.frameState->bIsOpaqueVisible;
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

						const auto& drawRange = frameCtx->GetFlashlightDrawRange();
						const auto& pipeline  = pass.pipelines[PIPE_ID_MAIN];

						ctx.profiler->AddFlashlightIndirect(1, drawRange.commandCount);

						pso.BeginRendering(cmd);

						pso.DrawIndexedIndirect(
							cmd,
							indirectBuffer,
							drawRange,
							pipeline,
							pass.pushWriter);

						pso.EndRendering(cmd);
					});
		});
}
