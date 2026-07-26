#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../../backend/ImageUtils.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"

namespace I = ImageUtils;

static constexpr size_t PIPE_ID_EDGES  = 0;
static constexpr size_t PIPE_ID_WEIGHT = 1;
static constexpr size_t PIPE_ID_BLEND  = 2;

void RegisterSMAAPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"SMAA",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::PostAA)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->InstancesActive() &&
							ctx.profiler->debugToggles.aaMode == static_cast<uint32_t>(RD::AntiAliasingMethod::AA_SMAA) &&
							ctx.frameState->CopyPostAAImage() &&
							!ctx.frameState->DebugRendering();
					})
				.ReadResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)

				.ReadResource(
					RD::Renderer_RenderTarget::Tonemap,
					RD::ImageAccess::Read)

				.InternalResource(
					RD::Renderer_RenderTarget::SMAAEdges,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				.InternalResource(
					RD::Renderer_RenderTarget::SMAAWeights,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::AAColor,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::SMAA,
							pass.passName);

						const auto& drawExtent = graph.GetDrawExtent();
						pass.scope = ComputeScope{{ drawExtent }};
						auto& pso = std::get<ComputeScope>(pass.scope);
						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& aaColor = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::AAColor);
						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& tonemap = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Tonemap);
						const auto& smaaEdges = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::SMAAEdges);
						const auto& smaaWeights = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::SMAAWeights);
						const auto linearLodClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::LinearLodClamp);
						const auto nearestClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);

						// ======================
						// Build Edges
						// ======================

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							tonemap,
							linearLodClampSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_2,
							depthResolved,
							nearestClampSampler,
							UINT32_MAX,
							RD::ImageAccess::DepthRead);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							smaaEdges);


						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_EDGES], pass.pushWriter);
						I::TransitionLayout(cmd, smaaEdges, RD::ImageAccess::Write, RD::ImageAccess::Read);

						// ======================
						// Weight blend
						// ======================

						// Only needed for weight stage to access bindless textures (area and search)
						pso.SetPush(ctx.profiler->smaaTexturesIds);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							smaaEdges,
							linearLodClampSampler);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							smaaWeights);
						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_WEIGHT], pass.pushWriter);
						I::TransitionLayout(cmd, smaaWeights, RD::ImageAccess::Write, RD::ImageAccess::Read);
						pso.ClearPush();

						// ========================
						// Neighbourhood Blending
						// ========================

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							tonemap,
							linearLodClampSampler);
						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_2,
							smaaWeights,
							nearestClampSampler);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							aaColor);

						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_BLEND], pass.pushWriter);
					});
		});
}
