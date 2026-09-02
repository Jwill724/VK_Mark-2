#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../../scene/Scene.h"
#include "ResourceTypes.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"

static constexpr size_t PIPE_ID_SSS = 0;

void RegisterContactShadowsPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Contact_Shadows",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.RunOnAsyncCompute()

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->IsScreenSpaceShadowsOn() &&
							ctx.frameState->InstancesActive() &&
							!ctx.frameState->IsWireframeOn();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)

				.WriteResource(RD::Renderer_RenderTarget::SSContactShadows,
					RD::ImageAccess::ComputeWrite,
					RD::ImageAccess::ComputeRead)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::ScreenSpaceContactShadows,
							pass.passName,
							ctx.threadSlot,
							ctx.scheduleInfo->queue);

						const auto& drawExtent = graph.GetRenderExtent();
						pass.scope = ComputeScope{{ drawExtent }};
						auto& pso = std::get<ComputeScope>(pass.scope);

						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& contactShadows = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::SSContactShadows);
						const auto pointBorderSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::PointBorder);

						const auto& dispatchList = ctx.scene->GetDispatchList();

						const auto& renderPixelSizes = ctx.scene->GetSceneData().renderPixelSizes;
						glm::vec2 invSize = glm::vec2(renderPixelSizes.x, renderPixelSizes.y);

						auto& sssPush = ctx.profiler->contactShadowsSettings;
						sssPush.lightCoords = dispatchList.lightCoords;
						sssPush.invDepthSize = invSize;
						pso.SetPush(sssPush);

						for (int i = 0; i < dispatchList.dispatchCount; i++)
						{
							const auto& disp = dispatchList.dispatch[i];
							pso.EditPush<SSSPush>(
								[disp](SSSPush& push)
								{
									push.waveOffsets = disp.waveOffset;
								});

							pso.UpdateWorkgroups({
								static_cast<uint32_t>(disp.waveCount[0]),
								static_cast<uint32_t>(disp.waveCount[1]),
								static_cast<uint32_t>(disp.waveCount[2])},
								true);

							pso.BindReadImage(
								pass.pushWriter,
								RD::PUSH_BINDING_READ_1,
								depthResolved,
								pointBorderSampler,
								UINT32_MAX,
								RD::ImageAccess::DepthRead);

							pso.BindWriteImage(
								pass.pushWriter,
								RD::PUSH_BINDING_WRITE_1,
								contactShadows);

							pso.DispatchComputePass(
								ctx.commandBuffer,
								pass.pipelines[PIPE_ID_SSS],
								pass.pushWriter);
						}
					});
		});
}
