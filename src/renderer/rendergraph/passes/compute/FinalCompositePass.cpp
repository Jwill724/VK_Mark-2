#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"
#include "../../../scene/Scene.h"

static constexpr size_t PIPE_ID_COMPOSITE = 0;

void RegisterFinalCompositePass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Final_Composite",
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

				.ReadResource(
					RD::Renderer_RenderTarget::VolumetricLight,
					RD::ImageAccess::Read)

				.ReadResource(
					RD::Renderer_RenderTarget::BloomMipchain,
					RD::ImageAccess::Read,
					0,
					VK_REMAINING_MIP_LEVELS)

				.ReadResource(
					RD::Renderer_RenderTarget::LensFlareColor,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::Tonemap,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)


				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::FinalComposite,
							pass.passName);

						const auto& drawExtent = graph.GetDrawExtent();
						pass.scope = ComputeScope{{ drawExtent }};
						auto& pso = std::get<ComputeScope>(pass.scope);

						const auto aaMode = static_cast<RD::AntiAliasingMethod>(ctx.profiler->debugToggles.aaMode);
						bool taaEnabled = (aaMode == RD::AntiAliasingMethod::AA_TAA && ctx.frameState->IsTemporalValid() && !ctx.frameState->DebugRendering());

						const auto& hdrScene = !taaEnabled
							? ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HDRScene)
							: ctx.imageTable->GetRenderTarget(TemporalHistory::GetColorHistorySlots(ctx.frameState->GetTemporalIndex()).write);

						const auto& volumetricLight = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::VolumetricLight);
						const auto& tonemap = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Tonemap);
						const auto& lensflare = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::LensFlareColor);
						const auto& bloom = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::BloomMipchain);
						const auto& dummy = ctx.imageTable->GetStaticTexture(RD::Renderer_Texture::Dummy);
						const auto linearClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::LinearClamp);
						const auto linearSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::Linear);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							tonemap);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							hdrScene,
							linearSampler);

						if (ctx.frameState->IsVolumetricsOn() &&
							ctx.scene->GetVolumetricShadowInfo().params.y != 0.0f &&
							!ctx.frameState->DebugRendering())
						{
							pso.BindReadImage(
								pass.pushWriter,
								RD::PUSH_BINDING_READ_2,
								volumetricLight,
								linearClampSampler);
						}
						else
						{
							pso.BindReadImage(
								pass.pushWriter,
								RD::PUSH_BINDING_READ_2,
								dummy,
								linearClampSampler);
						}

						if (ctx.profiler->debugToggles.enableLensFlare &&
							!ctx.profiler->enableWireframeView &&
							!ctx.frameState->DebugRendering())
						{
							pso.BindReadImage(
								pass.pushWriter,
								RD::PUSH_BINDING_READ_3,
								lensflare,
								linearClampSampler);
						}
						else
						{
							pso.BindReadImage(
								pass.pushWriter,
								RD::PUSH_BINDING_READ_3,
								dummy,
								linearClampSampler);
						}

						if (ctx.profiler->debugToggles.enableBloom &&
							!ctx.profiler->enableWireframeView &&
							!ctx.frameState->DebugRendering())
						{
							pso.BindReadImage(
								pass.pushWriter,
								RD::PUSH_BINDING_READ_4,
								bloom,
								linearClampSampler,
								0);
						}
						else
						{
							pso.BindReadImage(
								pass.pushWriter,
								RD::PUSH_BINDING_READ_4,
								dummy,
								linearClampSampler);
						}

						pso.DispatchComputePass(
							ctx.commandBuffer,
							pass.pipelines[PIPE_ID_COMPOSITE],
							pass.pushWriter);
					});
		});
}
