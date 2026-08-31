#include "pch.h"

#include "../../RenderPasses.h"
#include "../../scopes/GraphicsScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"
#include "../../../frame/FrameContext.h"
#include "../../../scene/Scene.h"

static constexpr size_t PIPE_ID_MAIN = 0;

void RegisterDirectionalCSMPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Sun_CSM",
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
							ctx.frameState->InstancesActive() &&
							!ctx.frameState->IsCSMAtlasCached() &&
							!ctx.frameState->DebugRenderFastPath() &&
							!ctx.frameState->RTShadowsEnabled();
					})

				.WriteResource(
					RD::Renderer_RenderTarget::DirectionalCSMAtlas,
					RD::ImageAccess::GraphicsDepthWrite,
					RD::ImageAccess::DepthRead)

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::DirectionalCSMAtlas,
							pass.passName);

						pass.scope = GraphicsScope{};
						auto& pso = std::get<GraphicsScope>(pass.scope);

						const auto& frameCtx = ctx.frameCtx;
						VkCommandBuffer cmd  = ctx.commandBuffer;

						const auto& atlas = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DirectionalCSMAtlas);
						AttachmentDesc depth{};
						depth.imageView   = atlas.m_imageView;
						depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
						depth.SetDepth(1);

						pso.UpdateRenderInfo(
							{ atlas.Width(), atlas.Height() },
							{ depth },
							true); // Atlas on

						const auto indirectCountBuffer =
							frameCtx->GetGPUBuffer(RD::Renderer_Buffer::IndirectDrawCounts).m_buffer;
						const auto taskDispatchBuffer =
							frameCtx->GetGPUBuffer(RD::Renderer_Buffer::TaskDispatch).m_buffer;

						const VkExtent2D atlasExtent = pso.GetAtlasExtent();
						const VkExtent2D tileExtent =
						{
							atlasExtent.width  / 2u,
							atlasExtent.height / 2u
						};

						const glm::vec4 lightDir =
							glm::vec4(glm::vec3(ctx.scene->GetSceneData().sunlightDirection), 0.0f);

						pso.BeginRendering(cmd);

						for (uint32_t cascadeIdx = 0; cascadeIdx < RD::MAX_SHADOW_CASCADES; ++cascadeIdx)
						{
							const uint32_t tileX = cascadeIdx % 2u;
							const uint32_t tileY = cascadeIdx / 2u;

							VkOffset2D offset =
							{
								static_cast<int32_t>(tileX * tileExtent.width),
								static_cast<int32_t>(tileY * tileExtent.height)
							};

							pso.UpdateAtlas(offset, tileExtent);
							pso.ApplyViewport(cmd);

							const uint32_t cascadeSlot = RD::VIS_SLOT_CSM0 + cascadeIdx;
							ASSERT(cascadeSlot < RD::VIS_SLOT_COUNT);

							const glm::mat4& cascadeVP = ctx.scene->GetCSMData().cascadeVP[cascadeIdx];

							DepthTaskPush push{};
							push.viewproj = cascadeVP;
							push.eye = lightDir;
							push.slot = cascadeSlot;
							pso.SetPush(push);

							pso.DrawMeshTasksIndirectCount(
								cmd,
								cascadeSlot,
								taskDispatchBuffer,
								indirectCountBuffer,
								pass.pipelines[PIPE_ID_MAIN],
								pass.pushWriter);
						}

						pso.EndRendering(cmd);
					});
		});
}
