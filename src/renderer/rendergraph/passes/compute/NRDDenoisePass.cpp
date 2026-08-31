#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../../backend/NRDContext.h"
#include "../../../backend/DescriptorManager.h"
#include "../../../backend/PipelineManager.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../../profiler/Profiler.h"
#include "../../../frame/FrameContext.h"

void RegisterNRDDenoisePass(RenderGraph& graph)
{
	graph.AddPass("NRD_Denoise", {},
		[&](RenderPassBuilder& builder)
		{
			builder
				.RunOnAsyncCompute()

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							(ctx.NRDReflect != nullptr && ctx.NRDReflect->IsValid() ||
							ctx.NRDShadow != nullptr && ctx.NRDShadow->IsValid()) &&
							ctx.frameState->IsNRDActive();
					})

				.ReadResource(RD::Renderer_RenderTarget::NRDMotion,                NRD_INPUT_ACCESS)
				.ReadResource(RD::Renderer_RenderTarget::NRDNormalRoughness,       NRD_INPUT_ACCESS)
				.ReadResource(RD::Renderer_RenderTarget::NRDViewZ,                 NRD_INPUT_ACCESS)
				.ReadResource(RD::Renderer_RenderTarget::ReflectRadiance,          NRD_INPUT_ACCESS)
				.ReadResource(RD::Renderer_RenderTarget::RTShadowPenumbra,         NRD_INPUT_ACCESS)
				.ReadResource(RD::Renderer_RenderTarget::NRDShadowNormalRoughness, NRD_INPUT_ACCESS)
				.ReadResource(RD::Renderer_RenderTarget::NRDShadowViewZ,           NRD_INPUT_ACCESS)
				.ReadResource(RD::Renderer_RenderTarget::Velocity,                 NRD_INPUT_ACCESS)

				.WriteResource(RD::Renderer_RenderTarget::RTReflectDenoised,
					NRD_OUTPUT_ACCESS, RD::ImageAccess::ComputeRead)
				.WriteResource(RD::Renderer_RenderTarget::RTShadowDenoised,
					NRD_OUTPUT_ACCESS, RD::ImageAccess::ComputeRead)

				.SetRecord([](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx, ctx.commandBuffer, RD::Renderer_Pass::NRDDenoise,
							pass.passName, ctx.threadSlot, ctx.scheduleInfo->queue);

						pass.scope = ComputeScope{{1,1}, WORKGROUP_NONE};
						auto& pso = std::get<ComputeScope>(pass.scope);

						if (ctx.frameState->RTShadowsEnabled())
							ctx.NRDShadow->RecordDispatches(ctx.commandBuffer, pso, pass.pushWriter);

						if (ctx.frameState->RTReflectionsEnabled())
							ctx.NRDReflect->RecordDispatches(ctx.commandBuffer, pso, pass.pushWriter);

						ctx.descriptorManager->BindDescriptorSetsCompute(
							ctx.commandBuffer,
							ctx.frameCtx->GetFrameSet(),
							ctx.pipelineManager->GetGlobalLayout());
					});
		});
}
