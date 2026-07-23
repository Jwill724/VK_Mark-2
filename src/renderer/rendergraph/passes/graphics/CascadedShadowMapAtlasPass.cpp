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

void RegisterDirectionalCSMPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Cascaded_Shadow_Map_Atlas",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->IsShadowsOn() &&
							ctx.frameState->InstancesActive() &&
							!ctx.frameState->DebugRenderFastPath();
					})

				.WriteResource(
					RD::Renderer_RenderTarget::DirectionalCSMAtlas,
					RD::ImageAccess::GraphicsDepthWrite,
					RD::ImageAccess::DepthRead)

				.SetSetup(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						pass.scope = GraphicsScope{};
						auto& pso = std::get<GraphicsScope>(pass.scope);

						const auto& atlas = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DirectionalCSMAtlas);

						AttachmentDesc depth{};
						depth.imageView = atlas.m_imageView;
						depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
						depth.SetDepth(1.0f);

						pso.UpdateRenderInfo(
							{
								atlas.Width(),
								atlas.Height()
							},
							{ depth },
							true); // Atlas on
					})

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::DirectionalCSMAtlas,
							pass.passName);

						auto& pso = std::get<GraphicsScope>(pass.scope);
						const auto& frameCtx = ctx.frameCtx;

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto indirectBuffer =
							frameCtx->GetGPUBuffer(RD::Renderer_Buffer::IndirectDraws).m_buffer;

						const auto indirectCountBuffer =
							frameCtx->GetGPUBuffer(RD::Renderer_Buffer::IndirectDrawCounts).m_buffer;

						const auto& pipeline = pass.pipelines[PIPE_ID_MAIN];

						const VkExtent2D atlasExtent = pso.GetAtlasExtent();
						const VkExtent2D tileExtent =
						{
							atlasExtent.width / 2u,
							atlasExtent.height / 2u
						};

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
							pso.SetPush(ctx.scene->GetCSMData().cascadeVP[cascadeIdx]);

							uint32_t cascadeIndex = RD::VIS_SLOT_CSM0 + cascadeIdx;
							ASSERT(cascadeIndex < RD::VIS_SLOT_COUNT);

							pso.DrawIndexedIndirectCount(
								cmd,
								cascadeIndex,
								indirectBuffer,
								indirectCountBuffer,
								pipeline,
								pass.pushWriter);
							}

						pso.EndRendering(cmd);
					});
		});
}
