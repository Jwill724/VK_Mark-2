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
				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->IsShadowsOn() &&
							ctx.frameState->IsScreenSpaceShadowsOn() &&
							ctx.frameState->InstancesActive() &&
							!ctx.frameState->IsWireframeOn();
					})

				.WriteResource(RD::Renderer_RenderTarget::SSContactShadows,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				.SetSetup(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						const auto& drawExtent = graph.GetDrawExtent();
						pass.scope = ComputeScope{{ drawExtent }};
						auto& pso = std::get<ComputeScope>(pass.scope);

						const auto& dispatchList = ctx.scene->GetDispatchList();

						const auto& pixelSizes = ctx.scene->GetSceneData().pixelSizes;
						glm::vec2 invSize = glm::vec2(pixelSizes.x, pixelSizes.y);

						auto& sssPush = ctx.profiler->contactShadowsSettings;
						sssPush.lightCoords = dispatchList.lightCoords;
						sssPush.invDepthSize = invSize;
						pso.SetPush(sssPush);
					})

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::ScreenSpaceContactShadows,
							pass.passName);

						auto& pso = std::get<ComputeScope>(pass.scope);

						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& contactShadows = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::SSContactShadows);
						const auto pointBorderSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::PointBorder);

						const auto& dispatchList = ctx.scene->GetDispatchList();
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
								pointBorderSampler);

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
