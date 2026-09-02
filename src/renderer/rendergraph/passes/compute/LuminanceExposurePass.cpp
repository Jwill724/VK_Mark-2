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
#include "../../../scene/Scene.h"

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
				.SetPhase(RenderPhase::PostProcess)
				.ForceExecution()

				.ReadResource(
					RD::Renderer_RenderTarget::HDRScene,
					RD::ImageAccess::Read)

				.HistoryResource(COLOR_RESOLVED_A, COLOR_RESOLVED_B,
					RD::ImageAccess::Read, RD::ImageAccess::Read, true, true)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::LuminanceExposure,
							pass.passName);

						const auto aaMode = static_cast<RD::AntiAliasingMethod>(ctx.profiler->debugToggles.aaMode);
						bool taaEnabled = (aaMode == RD::AntiAliasingMethod::AA_TAA && ctx.frameState->IsTemporalValid());

						uint32_t frameIndex = ctx.scene->GetSceneData().temporal.x;
						const auto& resolvedTaa = TemporalHistory::GetColorHistorySlots(static_cast<uint64_t>(frameIndex)).write;

						const auto& hdrScene = !taaEnabled
							? ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HDRScene)
							: ctx.imageTable->GetRenderTarget(resolvedTaa);

						const auto linearSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::Linear);

						const auto& luminanceBuf = ctx.bufferTable->GetGPUBuffer(RD::Renderer_Buffer::Luminance);

						pass.scope = ComputeScope{{ graph.GetDisplayExtent() }};
						auto& pso = std::get<ComputeScope>(pass.scope);

						// ==========================
						// Luminance Exposure Reduce
						// ==========================

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							hdrScene,
							linearSampler);

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
