#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../../profiler/Profiler.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../backend/BufferBarriers.h"
#include "../../../frame/FrameContext.h"

namespace B = BufferBarriers;

static constexpr size_t PIPE_ID_SHADOW_BOUNDS = 0;

void RegisterShadowBoundsPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Shadow_Bounds",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetSetup(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						pass.scope = ComputeScope{{ 1u, 1u }, { WORKGROUP_NONE }};
						auto& pso = std::get<ComputeScope>(pass.scope);
						pso.UpdateWorkgroups(WORKGROUP_1, true);

						const auto& hiz = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HiZ);
						const auto hizSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::HiZ);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							hiz,
							hizSampler,
							0u);
					})

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return ctx.frameState->bHiZValid &&
							ctx.profiler->debugToggles.enableShadows &&
							ctx.frameState->activeInstanceCount > 0;
					})

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::ShadowBounds,
							pass.passName);

						const auto& shadowBoundsBuffer = ctx.frameCtx->GetGPUBuffer(RD::Renderer_Buffer::ShadowCullData);

						auto& pso = std::get<ComputeScope>(pass.scope);
						VkCommandBuffer cmd = ctx.commandBuffer;
						pso.DispatchComputePass(
							cmd,
							pass.pipelines[PIPE_ID_SHADOW_BOUNDS],
							pass.pushWriter);

						B::ComputeWriteToRead(cmd, shadowBoundsBuffer);
					});
		});
}
