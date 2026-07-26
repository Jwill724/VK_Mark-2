#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../../scene/Scene.h"
#include "../../../backend/ImageUtils.h"
#include "ResourceTypes.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"

namespace I = ImageUtils;

static constexpr size_t PIPE_ID_HI_Z_PREFILTER = 0;
static constexpr size_t PIPE_ID_MAIN           = 1;
static constexpr size_t PIPE_ID_FILTER         = 2;
static constexpr size_t PIPE_ID_DENOISE        = 3;

void RegisterSSAOPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"SSAO",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.RunOnAsyncCompute()

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->InstancesActive() &&
							ctx.profiler->debugToggles.aoMode != static_cast<uint32_t>(RD::AmbientOcclusionMethod::AO_OFF) &&
							!ctx.frameState->IsWireframeOn();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)

				.ReadResource(
					RD::Renderer_RenderTarget::ViewSpaceNormals,
					RD::ImageAccess::ComputeRead)

				.InternalResource(
					RD::Renderer_RenderTarget::LinearizedMinHiZ,
					RD::ImageAccess::ComputeWrite,
					RD::ImageAccess::ComputeRead,
					0u, VK_REMAINING_MIP_LEVELS)

				.InternalResource(
					RD::Renderer_RenderTarget::AORaw,
					RD::ImageAccess::ComputeWrite,
					RD::ImageAccess::ComputeRead)

				.InternalResource(
					RD::Renderer_RenderTarget::AoEdgeInfo,
					RD::ImageAccess::ComputeWrite,
					RD::ImageAccess::ComputeRead)

				.InternalResource(
					RD::Renderer_RenderTarget::AOTemp,
					RD::ImageAccess::ComputeWrite,
					RD::ImageAccess::ComputeRead)

				.WriteResource(
					RD::Renderer_RenderTarget::BentNormals,
					RD::ImageAccess::ComputeWrite,
					RD::ImageAccess::ComputeRead)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::SSAO,
							pass.passName,
							ctx.threadSlot,
							ctx.scheduleInfo->queue);

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& drawExtent = graph.GetDrawExtent();
						pass.scope = ComputeScope{ drawExtent };
						auto& pso = std::get<ComputeScope>(pass.scope);

						auto& ssaoPush = ctx.profiler->ssaoSettings;
						const auto& sceneData = ctx.scene->GetSceneData();

						const auto& proj = sceneData.proj;
						glm::vec2 fullPixelSize = glm::vec2(sceneData.pixelSizes.x, sceneData.pixelSizes.y);

						ssaoPush.depthLinearizeMult = -proj[3][2];
						ssaoPush.depthLinearizeAdd  =  proj[2][2];
						if (ssaoPush.depthLinearizeMult * ssaoPush.depthLinearizeAdd < 0.0)
						{
							ssaoPush.depthLinearizeAdd = -ssaoPush.depthLinearizeAdd;
						}

						ssaoPush.tanHalfFov.x = 1.0f / proj[0][0];
						ssaoPush.tanHalfFov.y = 1.0f / proj[1][1];

						ssaoPush.ndcToViewMul = { ssaoPush.tanHalfFov.x * 2.0f, ssaoPush.tanHalfFov.y * -2.0f };
						ssaoPush.ndcToViewAdd = { ssaoPush.tanHalfFov.x * -1.0f, ssaoPush.tanHalfFov.y * 1.0f };

						ssaoPush.ndcToViewMul_x_PixelSize = ssaoPush.ndcToViewMul * fullPixelSize;

						ssaoPush.noiseIndex = sceneData.temporal.x % 64u;
						ssaoPush.isFinalPass = 0u; // Reset each frame

						ssaoPush.blurDirection = { 1.0f, 0.0f }; // Horizontal first

						pso.SetPush(ssaoPush);

						const auto& linearizedMinHiZ = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::LinearizedMinHiZ);
						const auto& rawAO            = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::AORaw);
						const auto& aoTemp           = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::AOTemp);
						const auto& edgeInfo         = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::AoEdgeInfo);
						const auto& bentNormals      = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::BentNormals);
						const auto& viewSpaceNormals = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::ViewSpaceNormals);
						const auto& depthResolved    = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);

						const auto nearestClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);
						const auto depthPyramidSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::HiZ);
						const auto aoSampler           = ctx.imageTable->GetSampler(RD::Renderer_Sampler::LinearLodClamp);
						const auto nearSampler         = ctx.imageTable->GetSampler(RD::Renderer_Sampler::Nearest);

						// ======================
						// HiZ Prefilter
						// ======================

						pso.BindReadImage(
							pass.pushWriter,
							RD::PUSH_BINDING_READ_1,
							depthResolved,
							nearestClampSampler,
							UINT32_MAX,
							RD::ImageAccess::DepthRead);

						uint32_t pushWriteBinding = RD::PUSH_BINDING_WRITE_1;
						for (uint32_t i = 0u; i < RD::HI_Z_MIP_COUNT; i++)
						{
							ASSERT(pushWriteBinding <= RD::PUSH_BINDING_WRITE_5);

							pso.BindWriteImage(
								pass.pushWriter,
								pushWriteBinding,
								linearizedMinHiZ,
								i);
							pushWriteBinding++;
						}

						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_HI_Z_PREFILTER], pass.pushWriter);

						I::TransitionLayoutCompute(cmd, linearizedMinHiZ,
							RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead,
							0u, linearizedMinHiZ.m_mipLevels);

						// ======================
						// SSAO Main
						// ======================

						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, linearizedMinHiZ, depthPyramidSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, viewSpaceNormals, nearestClampSampler);

						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, rawAO);
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_2, edgeInfo);
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_3, bentNormals);

						pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_MAIN], pass.pushWriter);

						I::TransitionLayoutCompute(cmd, rawAO,    RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead);
						I::TransitionLayoutCompute(cmd, edgeInfo, RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead);

						// ======================
						// Filter / Denoise setup
						// (shared bindings for both branches)
						// ======================

						pso.BindReadImage(pass.pushWriter,  RD::PUSH_BINDING_READ_1,  rawAO,    aoSampler);
						pso.BindReadImage(pass.pushWriter,  RD::PUSH_BINDING_READ_2,  edgeInfo, nearSampler);
						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, aoTemp);

						const bool useTAA = ctx.profiler->debugToggles.aaMode == static_cast<uint32_t>(RD::AntiAliasingMethod::AA_TAA);

						if (!useTAA)
						{
							// ======================
							// Filter Horizontal
							// ======================
							pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_FILTER], pass.pushWriter);

							I::TransitionLayoutCompute(cmd, aoTemp, RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead);
							I::TransitionLayoutCompute(cmd, rawAO,  RD::ImageAccess::ComputeRead,  RD::ImageAccess::ComputeWrite);

							// ======================
							// Filter Vertical
							// ======================

							pso.BindReadImage(pass.pushWriter,  RD::PUSH_BINDING_READ_1,  aoTemp,   aoSampler);
							pso.BindReadImage(pass.pushWriter,  RD::PUSH_BINDING_READ_2,  edgeInfo, nearSampler);
							pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, rawAO);

							pso.EditPush<SSAOPush>([](SSAOPush& push) {
								push.blurDirection = { 0.0f, 1.0f };
							});

							pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_FILTER], pass.pushWriter);
						}
						else
						{
							// ======================
							// Denoise Pass 1
							// ======================

							pso.UpdateWorkgroups({ 32u, 16u, 1u });
							pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_DENOISE], pass.pushWriter);

							I::TransitionLayoutCompute(cmd, aoTemp, RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead);
							I::TransitionLayoutCompute(cmd, rawAO,  RD::ImageAccess::ComputeRead,  RD::ImageAccess::ComputeWrite);

							// ======================
							// Denoise Pass 2
							// ======================

							pso.BindReadImage(pass.pushWriter,  RD::PUSH_BINDING_READ_1,  aoTemp,   aoSampler);
							pso.BindReadImage(pass.pushWriter,  RD::PUSH_BINDING_READ_2,  edgeInfo, nearSampler);
							pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, rawAO);

							pso.EditPush<SSAOPush>([](SSAOPush& push) {
								push.isFinalPass = 1u;
							});

							pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_DENOISE], pass.pushWriter);
						}

						// Final transition, rawAO -> Read for downstream passes
						I::TransitionLayoutCompute(cmd, rawAO, RD::ImageAccess::ComputeWrite, RD::ImageAccess::ComputeRead);
					});
		});
}
