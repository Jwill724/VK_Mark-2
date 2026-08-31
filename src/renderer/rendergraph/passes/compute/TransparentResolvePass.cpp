#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"

static constexpr size_t PIPE_ID_MAIN = 0;

void RegisterTransparentResolvePass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Transparent_Resolve",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::Lighting)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return ctx.frameState->InstancesActive();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::TransparentAccumulation,
					RD::ImageAccess::Read)

				.ReadResource(
					RD::Renderer_RenderTarget::TransparentRevealage,
					RD::ImageAccess::Read)

				.ReadResource(
					RD::Renderer_RenderTarget::TransparentVelocityAccum,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::TransparentResolved,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::TransparentVelocityResolved,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::TransparentResolve,
							pass.passName);

						const auto& drawExtent = graph.GetDrawExtent();
						pass.scope = ComputeScope{{ drawExtent }};
						auto& pso = std::get<ComputeScope>(pass.scope);

						const auto& transparentAccum = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::TransparentAccumulation);
						const auto& transparentVelocityAccum = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::TransparentVelocityAccum);
						const auto& transparentVelocityResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::TransparentVelocityResolved);
						const auto& transparentReveal = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::TransparentRevealage);
						const auto& transparentResolve = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::TransparentResolved);
						const auto nearestClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							transparentAccum,
							nearestClampSampler);
						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_2,
							transparentReveal,
							nearestClampSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_3,
							transparentVelocityAccum,
							nearestClampSampler);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							transparentResolve);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_2,
							transparentVelocityResolved);

						pso.DispatchComputePass(
							ctx.commandBuffer,
							pass.pipelines[PIPE_ID_MAIN],
							pass.pushWriter);
					});
		});
}
