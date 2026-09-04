#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../backend/memory/BindlessBDATable.h"
#include "../../../backend/BufferBarriers.h"
#include "../../../../profiler/Profiler.h"
#include "../../../backend/ImageUtils.h"

namespace I = ImageUtils;
namespace B = BufferBarriers;

static constexpr size_t PIPE_ID_SHADING_REDUCE = 0;
static constexpr size_t PIPE_ID_TAA            = 1;

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
				.SetPhase(RenderPhase::Temporal)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return ctx.frameState->TemporalActive() &&
							   ctx.frameState->InstancesActive() &&
							   ctx.frameState->IsTemporalValid();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)

				.ReadResource(
					RD::Renderer_RenderTarget::PrevDepthResolved,
					RD::ImageAccess::DepthRead)

				.ReadResource(
					RD::Renderer_RenderTarget::Velocity,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::HDRScene,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::ShadingSignalHalf,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::TransparentAccumulation,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::TransparentRevealage,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::TransparentVelocityAccum,
					RD::ImageAccess::ComputeRead)

				.HistoryResource(COLOR_RESOLVED_A, COLOR_RESOLVED_B,
					RD::ImageAccess::ComputeRead, RD::ImageAccess::ComputeRead, true, true)

				.HistoryResource(SHADING_LOW_RESOLVED_A, SHADING_LOW_RESOLVED_B,
					RD::ImageAccess::ComputeRead, RD::ImageAccess::ComputeRead, true, true)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx, ctx.commandBuffer, RD::Renderer_Pass::TAA, pass.passName);

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto colorSlots = TemporalHistory::GetColorHistorySlots(ctx.frameState->GetTemporalIndex());
						const auto& colorHistory = ctx.imageTable->GetRenderTarget(colorSlots.read);
						const auto& colorCurrent = ctx.imageTable->GetRenderTarget(colorSlots.write);

						const auto shadingLowSlots = TemporalHistory::GetShadingLowSlots(ctx.frameState->GetTemporalIndex());
						const auto& shadingLowHistory = ctx.imageTable->GetRenderTarget(shadingLowSlots.read);
						const auto& shadingLowCurrent = ctx.imageTable->GetRenderTarget(shadingLowSlots.write);

						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& prevDepthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PrevDepthResolved);
						const auto& velocity = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Velocity);
						const auto& hdrScene = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HDRScene);
						const auto& shadingSignalHalf = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::ShadingSignalHalf);
						const auto& transparentRevealage = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::TransparentRevealage);
						const auto& transparentAccum = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::TransparentAccumulation);
						const auto& transparentVelocityAccum = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::TransparentVelocityAccum);
						const auto taaHistorySampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::TaaHistory);
						const auto nearestClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);

						const auto& luminanceBuf = ctx.bufferTable->GetGPUBuffer(RD::Renderer_Buffer::Luminance);

						const auto& drawExtent = graph.GetRenderExtent();

						// ======================
						// Shading signal reduce
						// ======================

						const Extents2D eighthExtent = {
							(drawExtent.Width() + 7u) / 8u,
							(drawExtent.Height() + 7u) / 8u };

						pass.scope = ComputeScope{ { eighthExtent }, WORKGROUP_8x8 };

						auto& pso = std::get<ComputeScope>(pass.scope);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							shadingSignalHalf,
							nearestClampSampler);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							shadingLowCurrent);

						I::TransitionLayout(cmd, shadingLowCurrent, RD::ImageAccess::ComputeRead, RD::ImageAccess::ComputeWrite);
						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_SHADING_REDUCE], pass.pushWriter);
						I::TransitionLayout(cmd, shadingLowCurrent, RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead);

						// =========
						// TAA main
						// =========

						pso.SetPush(ctx.profiler->taaSettings);

						pso.UpdateExtent(drawExtent);
						pso.UpdateWorkgroups(WORKGROUP_16x16);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							hdrScene,
							nearestClampSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_2,
							colorHistory,
							taaHistorySampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_3,
							velocity,
							nearestClampSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_4,
							depthResolved,
							nearestClampSampler,
							UINT32_MAX,
							RD::ImageAccess::DepthRead);
						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_5,
							prevDepthResolved,
							nearestClampSampler,
							UINT32_MAX,
							RD::ImageAccess::DepthRead);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_6,
							transparentRevealage,
							nearestClampSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_7,
							transparentAccum,
							nearestClampSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_8,
							transparentVelocityAccum,
							nearestClampSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_9,
							shadingLowCurrent,
							taaHistorySampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_10,
							shadingLowHistory,
							taaHistorySampler);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							colorCurrent);

						I::TransitionLayout(cmd, colorCurrent, RD::ImageAccess::ComputeRead, RD::ImageAccess::ComputeWrite);
						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_TAA], pass.pushWriter);
						I::TransitionLayout(cmd, colorCurrent, RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead);
						B::ComputeReadToWrite(cmd, luminanceBuf);
					});
				});
}
