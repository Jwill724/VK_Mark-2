#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"

static constexpr size_t PIPE_ID_DEBUG = 0;

void RegisterGBufferDebugPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"GBuffer_Debug",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::PostProcess)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->IsVisibilityDeferred() &&
							ctx.frameState->InstancesActive() &&
							ctx.frameState->IsShadedOverlayOn();
					})

				.ReadResource(RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)
				.ReadResource(RD::Renderer_RenderTarget::MaterialAlbedoRough,
					RD::ImageAccess::Read)
				.ReadResource(RD::Renderer_RenderTarget::MaterialNormal,
					RD::ImageAccess::Read)
				.ReadResource(RD::Renderer_RenderTarget::MaterialMetal,
					RD::ImageAccess::Read)
				.ReadResource(RD::Renderer_RenderTarget::MaterialEmissive,
					RD::ImageAccess::Read)
				.ReadResource(RD::Renderer_RenderTarget::ViewSpaceNormals,
					RD::ImageAccess::Read)
				.ReadResource(RD::Renderer_RenderTarget::AORaw,
					RD::ImageAccess::Read)
				.ReadResource(RD::Renderer_RenderTarget::Visibility,
					RD::ImageAccess::Read)
				.ReadResource(RD::Renderer_RenderTarget::SSContactShadows,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::Tonemap,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						const auto& drawExtent = graph.GetDrawExtent();
						pass.scope = ComputeScope{{ drawExtent }, WORKGROUP_8x8 };
						auto& pso = std::get<ComputeScope>(pass.scope);

						const auto& albedoRough = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::MaterialAlbedoRough);
						const auto& normal = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::MaterialNormal);
						const auto& metal = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::MaterialMetal);
						const auto& emissive = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::MaterialEmissive);
						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& vsNormals = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::ViewSpaceNormals);
						const auto& tonemap = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Tonemap);
						const auto& aoRaw = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::AORaw);
						const auto& visibility = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Visibility);
						const auto& contactShadows = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::SSContactShadows);
						const auto nearestClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);

						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, visibility, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, albedoRough, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_3, normal, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_4, metal, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_5, emissive, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_6, depthResolved , nearestClampSampler, UINT32_MAX, RD::ImageAccess::DepthRead);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_7, aoRaw, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_8, contactShadows, nearestClampSampler);

						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, tonemap);

						pso.DispatchComputePass(
							ctx.commandBuffer,
							pass.pipelines[PIPE_ID_DEBUG],
							pass.pushWriter);
					});
		});
}
