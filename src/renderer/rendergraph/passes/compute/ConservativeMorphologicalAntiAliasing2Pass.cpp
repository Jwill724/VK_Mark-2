#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../../backend/BufferBarriers.h"
#include "../../../backend/ImageUtils.h"
#include "EngineTypes.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../backend/memory/BindlessBDATable.h"
#include "../../../../profiler/Profiler.h"
#include "../../../frame/FrameContext.h"

namespace B = BufferBarriers;
namespace I = ImageUtils;

static constexpr size_t PIPE_ID_BUILD_EDGES      = 0;
static constexpr size_t PIPE_ID_DISPATCH_ARGS    = 1;
static constexpr size_t PIPE_ID_PROCESS_SHAPES   = 2;
static constexpr size_t PIPE_ID_DEFERRED_RESOLVE = 3;

void RegisterCMAA2Pass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"CMAA2",
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
							ctx.profiler->debugToggles.aaMode == static_cast<uint32_t>(RD::AntiAliasingMethod::AA_CMAA2) &&
							ctx.frameState->CopyPostAAImage() &&
							!ctx.frameState->DebugRendering();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::Tonemap,
					RD::ImageAccess::Read)

				.InternalResource(RD::Renderer_RenderTarget::CMAA2WorkingEdges,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				.WriteResource(RD::Renderer_RenderTarget::AAColor,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::CMAA2,
							pass.passName);

						const auto& drawExtent = graph.GetDrawExtent();
						pass.scope = ComputeScope{{ drawExtent }, { WORKGROUP_NONE } };
						auto& pso = std::get<ComputeScope>(pass.scope);
						const auto& frameCtx = ctx.frameCtx;
						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& indirectArgs          = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::DispatchIndirectArgs);
						const auto& deferredHeads         = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::Cmaa2DeferredHeads);
						const auto& deferredItems         = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::Cmaa2DeferredItems);
						const auto& deferredLocations     = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::Cmaa2DeferredLocations);
						const auto& shapeCandidates       = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::Cmaa2ShapeCandidates);
						const auto& control               = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::Cmaa2Control);

						const auto& aaColor               = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::AAColor);
						const auto& tonemap               = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Tonemap);
						const auto& workingEdges          = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::CMAA2WorkingEdges);
						const auto nearestClampSampler    = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);

						const uint32_t quadCountX = (pso.GetDrawExtent().Width() + 1u) >> 1u;
						const uint32_t quadCountY = (pso.GetDrawExtent().Height() + 1u) >> 1u;
						const uint32_t groupsX = (quadCountX + 13u) / 14u; 
						const uint32_t groupsY = (quadCountY + 13u) / 14u;

						pso.UpdateWorkgroups({ groupsX, groupsY, 1 }, true);
						pso.SetPush(ctx.frameCtx->GetCMAA2Push());

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							tonemap,
							nearestClampSampler);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							workingEdges);

						// Buffers reset to zero
						pso.FillGpuBuffer(cmd, control);
						pso.FillGpuBuffer(cmd, deferredHeads, 0x7FFFFFFFu);


						// ============
						// Build Edges
						// ============
						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_BUILD_EDGES], pass.pushWriter);
						I::TransitionLayout(cmd, workingEdges, RD::ImageAccess::Write, RD::ImageAccess::Read);
						B::ComputeWriteToRead(cmd, control);
						B::ComputeWriteToRead(cmd, shapeCandidates);

						// =======================
						// Indirect arg compute 1
						// =======================
						pso.EditPush<CMAA2Push>([](CMAA2Push& push) {
							push.params.w = 0.0f;
						});

						// LAST TIME needing to set workgroups
						pso.UpdateWorkgroups(WORKGROUP_1);
						pso.UpdateExtent({1, 1});

						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_DISPATCH_ARGS], pass.pushWriter);
						B::ComputeWriteToIndirectRead(cmd, indirectArgs);

						// =================
						// Shape Candidates
						// =================

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							tonemap,
							nearestClampSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_2,
							workingEdges,
							nearestClampSampler);

						pso.SetIndirect(indirectArgs.m_buffer, RD::DISPATCH_CMAA2_SHAPES_OFFSET_BYTES);
						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_PROCESS_SHAPES], pass.pushWriter);
						B::ComputeWriteToRead(cmd, control);
						B::ComputeWriteToRead(cmd, deferredLocations);
						B::ComputeWriteToRead(cmd, deferredItems);
						B::ComputeWriteToRead(cmd, deferredHeads);
						pso.ClearIndirect();


						// =======================
						// Indirect arg compute 2
						// =======================
						pso.EditPush<CMAA2Push>([](CMAA2Push& push) {
							push.params.w = 1.0f;
						});
						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_DISPATCH_ARGS], pass.pushWriter);
						B::ComputeWriteToIndirectRead(cmd, indirectArgs);

						// =================
						// Deferred Resolve
						// =================

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							tonemap,
							nearestClampSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_2,
							workingEdges,
							nearestClampSampler);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							aaColor);

						pso.SetIndirect(indirectArgs.m_buffer, RD::DISPATCH_CMAA2_DEFERRED_OFFSET_BYTES);
						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_DEFERRED_RESOLVE], pass.pushWriter);
					});
		});
}
