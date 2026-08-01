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
#include "../../../scene/LightingSystem.h"

static constexpr size_t PIPE_ID_MAIN = 0;
static constexpr size_t PIPE_ID_MESH = 1;

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
				.SetPhase(RenderPhase::AsyncWindow)

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

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::FlashlightShadow,
							pass.passName);

						pass.scope = GraphicsScope{};
						auto& pso = std::get<GraphicsScope>(pass.scope);

						const auto& frameCtx = ctx.frameCtx;
						VkCommandBuffer cmd  = ctx.commandBuffer;

						const bool bMeshPath = ctx.frameState->IsMeshShaderPath();

						const auto indirectBuffer =
							frameCtx->GetGPUBuffer(RD::Renderer_Buffer::IndirectDraws).m_buffer;
						const auto indirectCountBuffer =
							frameCtx->GetGPUBuffer(RD::Renderer_Buffer::IndirectDrawCounts).m_buffer;
						const auto taskDispatchBuffer =
							frameCtx->GetGPUBuffer(RD::Renderer_Buffer::TaskDispatch).m_buffer;

						const auto& pipeline =
							bMeshPath ? pass.pipelines[PIPE_ID_MESH]
									  : pass.pipelines[PIPE_ID_MAIN];

						const auto& shadowmap =
							ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::FlashlightShadowMap);

						AttachmentDesc depth{};
						depth.imageView   = shadowmap.m_imageView;
						depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
						depth.SetDepth(1);

						pso.UpdateRenderInfo(
							{ shadowmap.Width(), shadowmap.Height() },
							{ depth });

						const glm::mat4& flashlightVP = ctx.scene->GetSceneData().flashlightVP;

						if (bMeshPath)
						{
							const glm::vec3 lightPos = LightingSystem::_mainFlashLight.position;

							DepthTaskPush push{};
							push.viewproj      = flashlightVP;
							push.eye           = glm::vec4(lightPos, 1.0f);
							push.slot          = RD::VIS_SLOT_FLASHLIGHT;
							push.cullDistance  = LightingSystem::_mainFlashLight.radius;
							pso.SetPush(push);
						}
						else
						{
							pso.SetPush(flashlightVP);
						}

						pso.BeginRendering(cmd);

						if (bMeshPath)
						{
							pso.DrawMeshTasksIndirectCount(
								cmd,
								RD::VIS_SLOT_FLASHLIGHT,
								taskDispatchBuffer,
								indirectCountBuffer,
								pipeline,
								pass.pushWriter);
						}
						else
						{
							pso.DrawIndexedIndirectCount(
								cmd,
								RD::VIS_SLOT_FLASHLIGHT,
								indirectBuffer,
								indirectCountBuffer,
								pipeline,
								pass.pushWriter);
						}

						pso.EndRendering(cmd);
					});
		});
}
