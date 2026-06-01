#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../../backend/BufferBarriers.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../backend/memory/BindlessBDATable.h"
#include "../../../../profiler/Profiler.h"

namespace B = BufferBarriers;

static constexpr size_t PIPE_ID_LUMA_REDUCE   = 0;
static constexpr size_t PIPE_ID_LUMA_FINALIZE = 1;

void RegisterLuminanceExposurePass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Luminance_Exposure",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.ReadResource(
					RD::Renderer_RenderTarget::Opaque,
					RD::ImageAccess::Read)
				.ReadResource(
					RD::Renderer_RenderTarget::ColorHistoryB,
					RD::ImageAccess::Read)
				.ReadResource(
					RD::Renderer_RenderTarget::TransparentResolved,
					RD::ImageAccess::Read)

				.SetSetup(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						const auto& drawExtent = graph.GetDrawExtent();
						pass.scope = ComputeScope{{ drawExtent }};
						auto& pso = std::get<ComputeScope>(pass.scope);

						const auto aaMode = static_cast<RD::AntiAliasingMethod>(ctx.profiler->debugToggles.aaMode);

						bool taaEnabled = (aaMode == RD::AntiAliasingMethod::AA_TAA && ctx.frameState->bTemporalValid);
						const auto& opaque = !taaEnabled ?
							ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Opaque) :
							ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::ColorHistoryB);

						const auto& transparent = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::TransparentResolved);
						const auto& dummy = ctx.imageTable->GetStaticTexture(RD::Renderer_Texture::Dummy);
						const auto linearSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::Linear);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							opaque,
							linearSampler);

						if (ctx.frameState->bIsTransparentVisible)
						{
							pso.BindReadImage(
								pass.pushWriter,
								RD::PUSH_BINDING_READ_2,
								transparent,
								linearSampler);
						}
						else
						{
							pso.BindReadImage(
								pass.pushWriter,
								RD::PUSH_BINDING_READ_2,
								dummy,
								linearSampler);
						}
					})

				.DisableCulling()
				.ForceExecution()

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::LuminanceExposure,
							pass.passName);

						const auto& luminanceBuf = ctx.bufferTable->GetGPUBuffer(RD::Renderer_Buffer::Luminance);

						auto& pso = std::get<ComputeScope>(pass.scope);

						// ==========================
						// Luminance Exposure Reduce
						// ==========================
						pso.DispatchComputePass(
							ctx.commandBuffer,
							pass.pipelines[PIPE_ID_LUMA_REDUCE],
							pass.pushWriter);
						B::ComputeWriteToRead(ctx.commandBuffer, luminanceBuf);

						// ====================
						// Luminance Finalize
						// ====================
						pso.UpdateExtent({ctx.profiler->lumaExposureSettings.totalLumaTiles, 1u});
						pso.UpdateWorkgroups(WORKGROUP_256);
						pso.SetPush(ctx.profiler->lumaExposureSettings);

						pso.DispatchComputePass(
							ctx.commandBuffer,
							pass.pipelines[PIPE_ID_LUMA_FINALIZE],
							pass.pushWriter);
						B::ComputeWriteToRead(ctx.commandBuffer, luminanceBuf);
					});
		});
}
