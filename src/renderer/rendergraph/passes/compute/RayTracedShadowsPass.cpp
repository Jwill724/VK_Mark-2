#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../../backend/NRDContext.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../../profiler/Profiler.h"

static constexpr size_t PIPE_ID_NRD_PREP = 0;
static constexpr size_t PIPE_ID_INTERSECT = 1;

void RegisterRTShadowsPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"RT_Shadows",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.RunOnAsyncCompute()

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.NRDShadow != nullptr && ctx.NRDShadow->IsValid() &&
							ctx.frameState->IsNRDActive() &&
							ctx.frameState->RTShadowsEnabled();
					})

				.ReadResource(RD::Renderer_RenderTarget::DepthResolved, RD::ImageAccess::DepthRead)
				.ReadResource(RD::Renderer_RenderTarget::GBufferAlbedoRough, RD::ImageAccess::ComputeRead)
				.ReadResource(RD::Renderer_RenderTarget::GBufferNormalMaterial, RD::ImageAccess::ComputeRead)
				.ReadResource(RD::Renderer_RenderTarget::ViewNormals, RD::ImageAccess::ComputeRead)
				.ReadResource(RD::Renderer_RenderTarget::Velocity, RD::ImageAccess::ComputeRead)

				.WriteResource(RD::Renderer_RenderTarget::RTShadowPenumbra,
					RD::ImageAccess::ComputeWrite, NRD_INPUT_ACCESS)

				.WriteResource(RD::Renderer_RenderTarget::NRDShadowNormalRoughness,
					RD::ImageAccess::ComputeWrite, NRD_INPUT_ACCESS)
				.WriteResource(RD::Renderer_RenderTarget::NRDShadowViewZ,
					RD::ImageAccess::ComputeWrite, NRD_INPUT_ACCESS)

				.WriteResource(RD::Renderer_RenderTarget::PrevVelocity, // Empty transition, or separate shader...choose
					RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeWrite)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::RTShadows,
							pass.passName,
							ctx.threadSlot,
							ctx.scheduleInfo->queue);

						VkCommandBuffer cmd = ctx.commandBuffer;
						const auto& frameCtx = ctx.frameCtx;

						pass.scope = ComputeScope{ graph.GetDrawExtent(), WORKGROUP_8x8 };
						auto& pso = std::get<ComputeScope>(pass.scope);

						const auto& depth = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& viewNormals = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::ViewNormals);
						const auto& albedoRough = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::GBufferAlbedoRough);
						const auto& normalMaterial = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::GBufferNormalMaterial);
						const auto& velocity = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Velocity);

						const auto nearestClamp = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);

						// =========
						// NRD Prep
						// =========

						pso.SetPush(ctx.profiler->nrdShadowPush);

						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, depth, nearestClamp,
							UINT32_MAX, RD::ImageAccess::DepthRead);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, normalMaterial, nearestClamp);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_3, albedoRough, nearestClamp);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_4, velocity, nearestClamp);

						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1,
							ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PrevVelocity)); // *empty write

						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_2,
							ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::NRDShadowNormalRoughness));
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_3,
							ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::NRDShadowViewZ));

						pso.DispatchComputePass(ctx.commandBuffer, pass.pipelines[PIPE_ID_NRD_PREP], pass.pushWriter);

						// ==============
						// Ray intersect
						// ==============

						pso.SetPush(ctx.profiler->rtShadowPush);

						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, depth, nearestClamp,
							UINT32_MAX, RD::ImageAccess::DepthRead);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, viewNormals, nearestClamp);

						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1,
							ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::RTShadowPenumbra));

						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_INTERSECT], pass.pushWriter);
					});
		});
}
