#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../Renderer.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../../backend/ImageUtils.h"
#include "EngineTypes.h"
#include "../../../scene/Scene.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"

namespace I = ImageUtils;

static constexpr size_t PIPE_ID_BRIGHT = 0;
static constexpr size_t PIPE_ID_GEN    = 1;

void RegisterLensFlarePass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines) // Quarter res of full draw extents
{
	graph.AddPass(
		"Lens_Flare",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.profiler->debugToggles.enableLensFlare &&
							ctx.frameState->InstancesActive() &&
							!ctx.frameState->DebugRendering();
					})

				.WriteResource(
					RD::Renderer_RenderTarget::LensFlareColor,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				// Only setups for the first bright pass
				.SetSetup(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						const auto& drawExtent = graph.GetDrawExtent();
						pass.scope = ComputeScope{{ drawExtent.Width() / 4, drawExtent.Height() / 4 }};
						auto& pso = std::get<ComputeScope>(pass.scope);

						auto& lensFlarePush = ctx.profiler->lensFlareSettings;

						const auto& brightFlare = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::FlareBright);
						lensFlarePush.outputRes = { static_cast<float>(brightFlare.Width()), static_cast<float>(brightFlare.Height()) };
						lensFlarePush.invOutputRes = 1.0f / lensFlarePush.outputRes;

						const auto& sceneData = ctx.scene->GetSceneData();

						glm::vec3 cameraWorldPos = glm::vec3(ctx.scene->GetCamera().GetPosition());
						glm::vec3 sunDirWorld = glm::normalize(glm::vec3(sceneData.sunlightDirection));
						glm::vec3 sunWorldPos = cameraWorldPos + sunDirWorld * 10000.0f;
						glm::vec4 clip = sceneData.proj * sceneData.view * glm::vec4(sunWorldPos, 1.0f);
						bool inFront = (clip.w > 0.0f);

						glm::vec3 ndc = glm::vec3(clip) / clip.w;
						glm::vec2 uv{};
						uv.x = ndc.x * 0.5f + 0.5f;
						uv.y = 0.5f - ndc.y * 0.5f;

						bool onScreen =
							(uv.x >= 0.0f && uv.x <= 1.0f) &&
							(uv.y >= 0.0f && uv.y <= 1.0f);

						lensFlarePush.sunUv = uv;
						lensFlarePush.sunVisible = (inFront && onScreen) ? 1.0f : 0.0f;

						pso.SetPush(lensFlarePush);

						const auto aaMode = static_cast<RD::AntiAliasingMethod>(ctx.profiler->debugToggles.aaMode);
						bool taaEnabled = (aaMode == RD::AntiAliasingMethod::AA_TAA && ctx.frameState->IsTemporalValid());

						const auto& opaque = !taaEnabled
							? ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Opaque)
							: ctx.imageTable->GetRenderTarget(TaaHistory::Resolved(ctx.scene->GetSceneData().temporal.x));

						const auto& transparent = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::TransparentResolved);
						const auto& volumetricLight = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::VolumetricLight);
						const auto& flareBright = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::FlareBright);
						const auto& dummy = ctx.imageTable->GetStaticTexture(RD::Renderer_Texture::Dummy);
						const auto linearClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::LinearClamp);
						const auto linearSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::Linear);

						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							flareBright);


						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							opaque,
							linearSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_2,
							transparent,
							linearSampler);

						if (ctx.frameState->InstancesActive() &&
							ctx.profiler->debugToggles.enableVolumetrics &&
							ctx.profiler->debugToggles.enableShadows)
						{
							pso.BindReadImage(
								pass.pushWriter,
								RD::PUSH_BINDING_READ_3,
								volumetricLight,
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
					})

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::LensFlare,
							pass.passName);

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& hiZ = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HiZ);
						const auto& flareBright = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::FlareBright);
						const auto& lensflareColor = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::LensFlareColor);
						const auto linearClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::LinearClamp);
						const auto hiZSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::HiZ);

						auto& pso = std::get<ComputeScope>(pass.scope);

						// =============
						// Flare bright
						// =============
						I::TransitionLayout(cmd, flareBright, RD::ImageAccess::Read, RD::ImageAccess::Write);
						pso.DispatchComputePass(
							cmd,
							pass.pipelines[PIPE_ID_BRIGHT],
							pass.pushWriter);
						I::TransitionLayout(cmd, flareBright, RD::ImageAccess::Write, RD::ImageAccess::Read);

						// ==========
						// Flare gen
						// ==========
						pso.BindWriteImage(
							pass.pushWriter,
							RD::PUSH_BINDING_WRITE_1,
							lensflareColor);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							hiZ,
							hiZSampler);

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_2,
							flareBright,
							linearClampSampler);

						pso.DispatchComputePass(
							cmd,
							pass.pipelines[PIPE_ID_GEN],
							pass.pushWriter);
					});
		});
}
