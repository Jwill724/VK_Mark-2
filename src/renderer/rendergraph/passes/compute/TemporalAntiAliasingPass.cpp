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
#include "../../../scene/Scene.h"
#include "../../../backend/ImageUtils.h"

namespace I = ImageUtils;
namespace B = BufferBarriers;

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
					RD::ImageAccess::Read)

				.ReadResource(
					RD::Renderer_RenderTarget::PrevVelocity,
					RD::ImageAccess::Read)

				.ReadResource(
					RD::Renderer_RenderTarget::ViewNormals,
					RD::ImageAccess::Read)

				.ReadResource(
					RD::Renderer_RenderTarget::PrevViewNormals,
					RD::ImageAccess::Read)

				.ReadResource(
					RD::Renderer_RenderTarget::Opaque,
					RD::ImageAccess::Read)

				.ReadResource(
					RD::Renderer_RenderTarget::TransparentRevealage,
					RD::ImageAccess::Read)

				.ReadResource(
					RD::Renderer_RenderTarget::TransparentVelocityResolved,
					RD::ImageAccess::Read)

				.HistoryResource(COLOR_RESOLVED_A, COLOR_RESOLVED_B,
					RD::ImageAccess::Read, RD::ImageAccess::Read, true, true)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx, ctx.commandBuffer, RD::Renderer_Pass::TAA, pass.passName);

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto slots = TemporalHistory::GetColorHistorySlots(ctx.frameState->GetTemporalIndex());
						const auto& history = ctx.imageTable->GetRenderTarget(slots.read);
						const auto& current = ctx.imageTable->GetRenderTarget(slots.write);

						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& prevDepthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PrevDepthResolved);
						const auto& velocity = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Velocity);
						const auto& prevVelocity = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PrevVelocity);
						const auto& viewNormals = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::ViewNormals);
						const auto& prevViewNormals = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PrevViewNormals);
						const auto& opaque = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Opaque);
						const auto& transparentRevealage = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::TransparentRevealage);
						const auto& transparentVelocityResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::TransparentVelocityResolved);
						const auto taaHistorySampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::TaaHistory);
						const auto nearestClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);

						const auto& luminanceBuf = ctx.bufferTable->GetGPUBuffer(RD::Renderer_Buffer::Luminance);

						const auto& drawExtent = graph.GetDrawExtent();
						pass.scope = ComputeScope{{ drawExtent }};
						auto& pso = std::get<ComputeScope>(pass.scope);
						pso.SetPush(ctx.profiler->taaSettings);

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
							nearestClampSampler,
							UINT32_MAX,
							RD::ImageAccess::DepthRead);
						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_6,
							prevDepthResolved,
							nearestClampSampler,
							UINT32_MAX,
							RD::ImageAccess::DepthRead);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_7,
							viewNormals,
							nearestClampSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_8,
							prevViewNormals,
							nearestClampSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_9,
							transparentRevealage,
							nearestClampSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_10,
							transparentVelocityResolved,
							nearestClampSampler);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							current);

						I::TransitionLayout(cmd, current, RD::ImageAccess::Read, RD::ImageAccess::Write);
						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_MAIN], pass.pushWriter);
						I::TransitionLayout(cmd, current, RD::ImageAccess::Write, RD::ImageAccess::Read);
						B::ComputeReadToWrite(cmd , luminanceBuf);
					});
				});
}
