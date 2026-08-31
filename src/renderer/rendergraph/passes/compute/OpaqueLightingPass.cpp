#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"
#include "../../../backend/ImageUtils.h"

namespace I = ImageUtils;

static constexpr size_t PIPE_ID_MAIN = 0;

void RegisterOpaqueLightingPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Opaque_Lighting",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::Lighting)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->InstancesActive() &&
							!ctx.frameState->IsWireframeOn();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::Visibility,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::GBufferAlbedoRough,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::GBufferNormalMaterial,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::GBufferEmissive,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)

				.ReadResource(
					RD::Renderer_RenderTarget::BentNormalAO,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::SSContactShadows,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::RTReflectDenoised,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::RTShadowDenoised,
					RD::ImageAccess::ComputeRead)

				.ReadResource(
					RD::Renderer_RenderTarget::IndirectSSGI,
					RD::ImageAccess::ComputeRead)

				.WriteResource(
					RD::Renderer_RenderTarget::Opaque,
					RD::ImageAccess::ComputeWrite,
					RD::ImageAccess::ComputeRead)

				.HistoryResource(RADIANCE_RESOLVED_A, RADIANCE_RESOLVED_B,
					RD::ImageAccess::ComputeRead, RD::ImageAccess::ComputeRead, true, true)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::OpaqueLighting,
							pass.passName);

						const auto& drawExtent = graph.GetDrawExtent();
						pass.scope = ComputeScope{{ drawExtent }};
						auto& pso = std::get<ComputeScope>(pass.scope);

						VkCommandBuffer cmd = ctx.commandBuffer;

						pso.SetPush(ctx.profiler->forwardPush);

						const auto diffSlots = TemporalHistory::GetDiffuseRadianceSlots(ctx.frameState->GetTemporalIndex());
						const auto& diffuseRadiance = ctx.imageTable->GetRenderTarget(diffSlots.write);
						const auto& opaque = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Opaque);
						const auto& indirectSSGI = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::IndirectSSGI);
						const auto& visibility = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Visibility);
						const auto& albedoRough = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::GBufferAlbedoRough);
						const auto& normalMat = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::GBufferNormalMaterial);
						const auto& emissive = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::GBufferEmissive);
						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& viewNormals = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::ViewNormals);
						const auto& bentNormalAo = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::BentNormalAO);
						const auto& contactShadows = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::SSContactShadows);
						const auto& rtReflectDenoised = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::RTReflectDenoised);
						const auto& rtShadowDenoised = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::RTShadowDenoised);
						const auto linearClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::LinearClamp);
						const auto nearestClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);

						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, albedoRough, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, normalMat, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_3, emissive, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_4, depthResolved, nearestClampSampler, UINT32_MAX, RD::ImageAccess::DepthRead);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_5, viewNormals , nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_6, contactShadows, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_7, bentNormalAo, linearClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_8, indirectSSGI, linearClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_9, rtReflectDenoised, linearClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_10, rtShadowDenoised, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_11, visibility, nearestClampSampler);

						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, opaque);
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_2, diffuseRadiance);

						I::TransitionLayout(cmd, diffuseRadiance,
							RD::ImageAccess::ComputeRead, RD::ImageAccess::ComputeWrite);

						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_MAIN], pass.pushWriter);

						I::TransitionLayout(cmd, diffuseRadiance,
							RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead);
					});
		});
}
