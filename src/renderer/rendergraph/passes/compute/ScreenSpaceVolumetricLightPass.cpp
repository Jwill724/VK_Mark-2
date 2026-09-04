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
#include "../../../scene/Scene.h"

namespace I = ImageUtils;

void RegisterVolumetricLightPass(RenderGraph& graph)
{
	graph.AddPass(
		"God_Rays",
		{ RP::VolumetricLight, RP::VolumetricLightResolve, RP::VolumetricLightBlur },
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::AsyncWindow)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->IsVolumetricsOn() &&
							ctx.frameState->InstancesActive() &&
							!ctx.frameState->DebugRendering();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)

				.ReadResource(
					RD::Renderer_RenderTarget::PrevDepthResolved,
					RD::ImageAccess::DepthRead)

				.InternalResource(
					RD::Renderer_RenderTarget::VolumetricLight,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				.InternalResource(
					RD::Renderer_RenderTarget::VolumetricLightBlur,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				.HistoryResource(VOL_LIGHT_RESOLVED_A, VOL_LIGHT_RESOLVED_B,
					RD::ImageAccess::Read, RD::ImageAccess::Read, true, true)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::VolumetricLight,
							pass.passName);

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& drawExtent = graph.GetRenderExtent();
						const Extents2D halfExtent = {
							(drawExtent.Width() + 1u) / 2u,
							(drawExtent.Height() + 1u) / 2u };
						pass.scope = ComputeScope{{ halfExtent }};
						auto& pso = std::get<ComputeScope>(pass.scope);

						ctx.profiler->volLightSettings.blurDirection = { 1.0f, 0.0f }; // Start horizontal
						pso.SetPush(ctx.profiler->volLightSettings);

						const auto volSlots = TemporalHistory::GetVolLightHistorySlots(ctx.frameState->GetTemporalIndex());

						const auto& volHistoryRead = ctx.imageTable->GetRenderTarget(volSlots.read);
						const auto& volHistoryWrite = ctx.imageTable->GetRenderTarget(volSlots.write);

						const auto& volumetricLight = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::VolumetricLight);
						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& prevDepthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PrevDepthResolved);
						const auto& volumetricBlur = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::VolumetricLightBlur);
						const auto nearestClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);
						const auto linearClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::LinearClamp);

						// ======================
						// Volumetric Light
						// ======================

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							depthResolved,
							nearestClampSampler,
							UINT32_MAX,
							RD::ImageAccess::DepthRead);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							volumetricLight);

						pso.DispatchComputePass(cmd, ctx.Pipe(RP::VolumetricLight), pass.pushWriter);
						I::TransitionLayout(cmd, volumetricLight, RD::ImageAccess::Write, RD::ImageAccess::Read);

						// ======================
						// Temporal Resolve
						// ======================

						I::TransitionLayout(cmd, volHistoryWrite, RD::ImageAccess::Read, RD::ImageAccess::Write);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							depthResolved,
							nearestClampSampler,
							UINT32_MAX,
							RD::ImageAccess::DepthRead);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_2,
							volumetricLight,
							linearClampSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_3,
							volHistoryRead,
							linearClampSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_4,
							prevDepthResolved,
							nearestClampSampler,
							UINT32_MAX,
							RD::ImageAccess::DepthRead);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							volHistoryWrite);

						pso.DispatchComputePass(cmd, ctx.Pipe(RP::VolumetricLightResolve), pass.pushWriter);
						I::TransitionLayout(cmd, volHistoryWrite, RD::ImageAccess::Write, RD::ImageAccess::Read);

						// ======================
						// Blur horizontal
						// ======================

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							depthResolved,
							nearestClampSampler,
							UINT32_MAX,
							RD::ImageAccess::DepthRead);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_2,
							volHistoryWrite,
							linearClampSampler);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							volumetricBlur);
						pso.DispatchComputePass(cmd, ctx.Pipe(RP::VolumetricLightBlur), pass.pushWriter);
						I::TransitionLayout(cmd, volumetricBlur, RD::ImageAccess::Write, RD::ImageAccess::Read);

						// ======================
						// Blur vertical
						// ======================
						I::TransitionLayout(cmd, volumetricLight, RD::ImageAccess::Read, RD::ImageAccess::Write);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							depthResolved,
							nearestClampSampler,
							UINT32_MAX,
							RD::ImageAccess::DepthRead);

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

						pso.DispatchComputePass(cmd, ctx.Pipe(RP::VolumetricLightBlur), pass.pushWriter);
						I::TransitionLayout(cmd, volumetricLight, RD::ImageAccess::Write, RD::ImageAccess::Read);
					});
		});
}
