#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "EngineTypes.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"

static constexpr size_t PIPE_ID_MAIN  = 0;

// TODO: Look into the logic for color history indexing

void RegisterTAAPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"TAA",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.ReadResource(RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)
				.ReadResource(RD::Renderer_RenderTarget::PrevDepthResolved,
					RD::ImageAccess::DepthRead)
				.ReadResource(RD::Renderer_RenderTarget::Opaque,
					RD::ImageAccess::Read)
				.ReadResource(RD::Renderer_RenderTarget::ColorHistoryA,
					RD::ImageAccess::Read)
				.ReadResource(RD::Renderer_RenderTarget::Velocity,
					RD::ImageAccess::Read)
				.ReadResource(RD::Renderer_RenderTarget::PrevVelocity,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::ColorHistoryB,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				.SetSetup(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						const auto& drawExtent = graph.GetDrawExtent();
						pass.scope = ComputeScope{{ drawExtent }};
						auto& pso = std::get<ComputeScope>(pass.scope);

						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& prevDepthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PrevDepthResolved);
						const auto& velocity = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Velocity);
						const auto& prevVelocity = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PrevVelocity);
						const auto& opaque = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Opaque);
						const auto& colorHistoryA = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::ColorHistoryA);
						const auto& colorHistoryB = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::ColorHistoryB);
						const auto taaHistorySampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::TaaHistory);
						const auto nearestClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							opaque,
							nearestClampSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_2,
							colorHistoryA,
							taaHistorySampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_3,
							velocity,
							nearestClampSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_4,
							prevVelocity,
							taaHistorySampler);
						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_5,
							depthResolved,
							nearestClampSampler);
						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_6,
							prevDepthResolved,
							nearestClampSampler);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							colorHistoryB);
					})

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return ctx.frameState->bHasVisibles &&
							ctx.frameState->bTemporalValid &&
							ctx.profiler->debugToggles.aaMode == static_cast<uint32_t>(RD::AntiAliasingMethod::AA_TAA);
					})

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::TAA,
							pass.passName);

						auto& pso = std::get<ComputeScope>(pass.scope);
						pso.DispatchComputePass(ctx.commandBuffer, pass.pipelines[PIPE_ID_MAIN], pass.pushWriter);
					});
		});
}
