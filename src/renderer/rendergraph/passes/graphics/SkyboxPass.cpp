#include "pch.h"

#include "../../RenderPasses.h"
#include "../../scopes/GraphicsScope.h"
#include "../../../scene/Scene.h"
#include "ResourceTypes.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"

static constexpr size_t PIPE_ID_MAIN = 0;

void RegisterSkyboxPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Skybox",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::Lighting)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return !ctx.frameState->DebugRenderFastPath();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)

				.WriteResource(
					RD::Renderer_RenderTarget::HDRScene,
					RD::ImageAccess::GraphicsColorWrite,
					RD::ImageAccess::ComputeWrite)

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::Skybox,
							pass.passName);

						pass.scope = GraphicsScope{};
						auto& pso = std::get<GraphicsScope>(pass.scope);

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& pipeline = pass.pipelines[PIPE_ID_MAIN];

						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& skybox = ctx.imageTable->GetEnvironmentSet(ctx.profiler->debugToggles.activeEnvMap).skybox;
						const auto& hdrScene = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HDRScene);

						AttachmentDesc opaqueAttach{};
						opaqueAttach.imageView = hdrScene.m_imageView;
						opaqueAttach.loadOp    = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
						opaqueAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

						AttachmentDesc depthAttach{};
						depthAttach.imageView = depthResolved.m_imageView;
						depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
						depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
						depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_NONE;
						depthAttach.SetDepth(0);

						pso.UpdateRenderInfo(
							{
								hdrScene.Width(),
								hdrScene.Height()
							},
							{ opaqueAttach, depthAttach });

						const auto& sceneData = ctx.scene->GetSceneData();
						const auto& projUnjittered = ctx.scene->GetCurrentProjUnjittered();

						glm::mat4 proj{};
						if (ctx.frameState->InstancesActive() &&
							ctx.frameState->IsTaaOn())
						{
							proj = sceneData.proj;
						}
						else
						{
							proj = projUnjittered;
						}

						glm::mat4 view = glm::mat4(glm::mat3(sceneData.view)); // strip translation
						glm::mat4 viewproj = proj * view;
						glm::mat4 invVp = glm::inverse(viewproj);

						auto& push = ctx.profiler->skyboxPush;

						push.invVp = invVp;
						push.skyboxID = skybox.m_bindlessID;

						pso.SetPush(push);

						pso.BeginRendering(cmd);

						pso.DrawTriangle(cmd, pipeline);

						pso.EndRendering(cmd);
					});
		});
}
