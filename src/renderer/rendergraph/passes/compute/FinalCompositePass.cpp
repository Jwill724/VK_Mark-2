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
				.WriteResource(
					RD::Renderer_RenderTarget::Tonemap,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				// Possible cmaa2 write
				.WriteResource(
					RD::Renderer_RenderTarget::AAColor,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				.SetSetup(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						const auto& drawExtent = graph.GetDrawExtent();
						pass.scope = ComputeScope{{ drawExtent }};
						auto& scope = std::get<ComputeScope>(pass.scope);

						const auto aaMode = static_cast<RD::AntiAliasingMethod>(ctx.profiler->debugToggles.aaMode);
						bool taaEnabled = (aaMode == RD::AntiAliasingMethod::AA_TAA && ctx.frameState->bTemporalValid);

						const auto& opaque = !taaEnabled
							? ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Opaque)
							: ctx.imageTable->GetRenderTarget(TaaHistory::Resolved(static_cast<uint64_t>(ctx.scene->GetSceneData().temporal.x)));

						const auto& aaColor = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::AAColor);
						const auto& transparent = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::TransparentResolved);
						const auto& volumetricLight = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::VolumetricLight);
						const auto& tonemap = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Tonemap);
						const auto& lensflare = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::LensFlareColor);
						const auto& bloom = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::BloomMipchain);
						const auto& dummy = ctx.imageTable->GetStaticTexture(RD::Renderer_Texture::Dummy);
						const auto linearClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::LinearClamp);
						const auto linearSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::Linear);

						scope.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							tonemap);

						if (ctx.frameState->bCopyPostAAImage &&
							ctx.profiler->debugToggles.aaMode == static_cast<uint32_t>(RD::AntiAliasingMethod::AA_CMAA2))
						{
							scope.BindWriteImage(
								pass.pushWriter,
								RD::PUSH_BINDING_WRITE_2,
								aaColor);
						}
						else
						{
							scope.BindWriteImage(
								pass.pushWriter,
								RD::PUSH_BINDING_WRITE_2,
								tonemap);
						}

						scope.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							opaque,
							linearSampler);

						scope.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_2,
							transparent,
							linearSampler);

						if (ctx.profiler->debugToggles.enableVolumetrics &&
							ctx.profiler->debugToggles.enableShadows)
						{
							scope.BindReadImage(
								pass.pushWriter,
								RD::PUSH_BINDING_READ_3,
								volumetricLight,
								linearClampSampler);
						}
						else
						{
							scope.BindReadImage(
								pass.pushWriter,
								RD::PUSH_BINDING_READ_3,
								dummy,
								linearClampSampler);
						}

						if (ctx.profiler->debugToggles.enableLensFlare)
						{
							scope.BindReadImage(
								pass.pushWriter,
								RD::PUSH_BINDING_READ_4,
								lensflare,
								linearClampSampler);
						}
						else
						{
							scope.BindReadImage(
								pass.pushWriter,
								RD::PUSH_BINDING_READ_4,
								dummy,
								linearClampSampler);
						}

						if (ctx.profiler->debugToggles.enableBloom)
						{
							scope.BindReadImage(
								pass.pushWriter,
								RD::PUSH_BINDING_READ_5,
								bloom,
								linearClampSampler,
								0);
						}
						else
						{
							scope.BindReadImage(
								pass.pushWriter,
								RD::PUSH_BINDING_READ_5,
								dummy,
								linearClampSampler);
						}
					})

				.DisableCulling()
				.ForceExecution()

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::FinalComposite,
							pass.passName);

						auto& pso = std::get<ComputeScope>(pass.scope);
						pso.DispatchComputePass(
							ctx.commandBuffer,
							pass.pipelines[PIPE_ID_COMPOSITE],
							pass.pushWriter);
					});
		});
}
