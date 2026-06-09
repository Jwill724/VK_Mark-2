#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"
#include "../../../scene/Scene.h"
#include "../../../backend/ImageUtils.h"

namespace I = ImageUtils;

static constexpr size_t PIPE_ID_MAIN  = 0;

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
				.SetSetup(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						const auto& drawExtent = graph.GetDrawExtent();
						pass.scope = ComputeScope{{ drawExtent }};
						auto& pso = std::get<ComputeScope>(pass.scope);

						const auto slots = TaaHistory::Resolve(static_cast<uint64_t>(ctx.scene->GetSceneData().temporal.x));
						const auto& history = ctx.imageTable->GetRenderTarget(slots.read);
						const auto& current = ctx.imageTable->GetRenderTarget(slots.write);

						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& prevDepthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PrevDepthResolved);
						const auto& velocity = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Velocity);
						const auto& prevVelocity = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PrevVelocity);
						const auto& opaque = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Opaque);
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
							history,
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
							current);
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
					*ctx.frameCtx, ctx.commandBuffer, RD::Renderer_Pass::TAA, pass.passName);

				VkCommandBuffer cmd = ctx.commandBuffer;

				const auto slots    = TaaHistory::Resolve(ctx.scene->GetSceneData().temporal.x);
				const auto& current = ctx.imageTable->GetRenderTarget(slots.write);

				auto& pso = std::get<ComputeScope>(pass.scope);

				I::TransitionLayout(cmd, current, RD::ImageAccess::Read, RD::ImageAccess::Write);
				pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_MAIN], pass.pushWriter);
				I::TransitionLayout(cmd, current, RD::ImageAccess::Write, RD::ImageAccess::Read);
			});
		});
}
