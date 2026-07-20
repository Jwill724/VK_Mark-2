#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../../backend/ImageUtils.h"
#include "EngineTypes.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"

namespace I = ImageUtils;

static constexpr size_t PIPE_ID_MAIN = 0;
static constexpr size_t PIPE_ID_BLUR = 1;

void RegisterVolumetricLightPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines) // Half res of full draw extents
{
	graph.AddPass(
		"Volumetric_Lights",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetSetup(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						const auto& drawExtent = graph.GetDrawExtent();
						pass.scope = ComputeScope{{ drawExtent.Width() / 2, drawExtent.Height() / 2 }};
						auto& pso = std::get<ComputeScope>(pass.scope);

						ctx.profiler->volLightSettings.blurDirection = { 1.0f, 0.0f }; // Start horizontal
						pso.SetPush(ctx.profiler->volLightSettings);

						const auto& volumetricLight = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::VolumetricLight);
						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto nearestClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							depthResolved,
							nearestClampSampler);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							volumetricLight);
					})

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						const auto& debug = ctx.profiler->debugToggles;
						return ctx.frameState->activeInstanceCount > 0 &&
							debug.enableVolumetrics && debug.enableShadows;
					})

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::VolumetricLighting,
							pass.passName);

						auto& pso = std::get<ComputeScope>(pass.scope);
						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& volumetricLight = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::VolumetricLight);
						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& volumetricBlur = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::VolumetricLightBlur);
						const auto nearestClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);
						const auto linearClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::LinearClamp);

						// ======================
						// Volumetric Light
						// ======================
						I::TransitionLayout(cmd, volumetricLight, RD::ImageAccess::Read, RD::ImageAccess::Write);
						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_MAIN], pass.pushWriter);
						I::TransitionLayout(cmd, volumetricLight, RD::ImageAccess::Write, RD::ImageAccess::Read);

						// ======================
						// Blur horizontal
						// ======================
						I::TransitionLayout(cmd, volumetricBlur, RD::ImageAccess::Read, RD::ImageAccess::Write);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							depthResolved,
							nearestClampSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_2,
							volumetricLight,
							linearClampSampler);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							volumetricBlur);
						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_BLUR], pass.pushWriter);
						I::TransitionLayout(cmd, volumetricBlur, RD::ImageAccess::Write, RD::ImageAccess::Read);

						// ======================
						// Blur vertical
						// ======================
						I::TransitionLayout(cmd, volumetricLight, RD::ImageAccess::Read, RD::ImageAccess::Write);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							depthResolved,
							nearestClampSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_2,
							volumetricBlur,
							linearClampSampler);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							volumetricLight);

						pso.EditPush<VolumetricPush>(
						[](VolumetricPush& push)
						{
							push.blurDirection = { 0.0f, 1.0f }; // Vertical
						});

						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_BLUR], pass.pushWriter);
						I::TransitionLayout(cmd, volumetricLight, RD::ImageAccess::Write, RD::ImageAccess::Read);
					});
		});
}
