#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"

static constexpr size_t PIPE_ID_MAIN = 0;

void RegisterChromaticAberrationPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Chromatic_Aberration",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.profiler->debugToggles.enableChromaticAberration &&
							ctx.frameState->InstancesActive() &&
							!ctx.frameState->DebugRendering();
					})

				.WriteResource(
					RD::Renderer_RenderTarget::PostNonAAComposite,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				.SetSetup(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						const auto& drawExtent = graph.GetDrawExtent();
						pass.scope = ComputeScope{{ drawExtent }};
						auto& pso = std::get<ComputeScope>(pass.scope);

						const auto& aaColor = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::AAColor);
						const auto& tonemap = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Tonemap);
						const auto& postNonAA = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PostNonAAComposite);
						const auto linearClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::LinearClamp);

						const auto aaMode = static_cast<RD::AntiAliasingMethod>(ctx.profiler->debugToggles.aaMode);

						//------------------------------------
						// BIND INPUTS
						//------------------------------------
						if (aaMode != RD::AntiAliasingMethod::AA_OFF && aaMode != RD::AntiAliasingMethod::AA_TAA)
						{
							ASSERT(ctx.frameState->CopyPostAAImage());
							pso.BindReadImage(
								pass.pushWriter,
								RD::PUSH_BINDING_READ_1,
								aaColor,
								linearClampSampler);
						}
						else
						{
							pso.BindReadImage(
								pass.pushWriter,
								RD::PUSH_BINDING_READ_1,
								tonemap,
								linearClampSampler);
						}

						//------------------------------------
						// BIND OUTPUT
						//------------------------------------
						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							postNonAA);
					})

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::ChromaticAberration,
							pass.passName);

						auto& pso = std::get<ComputeScope>(pass.scope);
						pso.DispatchComputePass(
							ctx.commandBuffer,
							pass.pipelines[PIPE_ID_MAIN],
							pass.pushWriter);
					});
		});
}
