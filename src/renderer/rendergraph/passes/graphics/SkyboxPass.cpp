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
				.WriteResource(
					RD::Renderer_RenderTarget::Opaque,
					RD::ImageAccess::GraphicsColorWrite,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::DepthRaw,
					RD::ImageAccess::GraphicsDepthWrite,
					RD::ImageAccess::GraphicsDepthWrite)

				.SetSetup(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						pass.scope = GraphicsScope{};
						auto& pso = std::get<GraphicsScope>(pass.scope);

						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& skybox = ctx.imageTable->GetEnvironmentSet(ctx.profiler->debugToggles.activeEnvMap).skybox;
						const auto& depthRaw = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthRaw);
						const auto& opaque = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Opaque);

						AttachmentDesc opaqueAttach{};
						opaqueAttach.imageView = opaque.m_imageView;
						opaqueAttach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

						AttachmentDesc depthAttach{};
						depthAttach.imageView = depthResolved.m_imageView;
						depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
						depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
						depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
						depthAttach.clearValue.depthStencil.depth = 0.0f;

						if (!ctx.frameState->bHasVisibles || ctx.profiler->enableWireframeView) // Need depth write since no prepass occured
						{
							depthAttach.imageView = depthRaw.m_imageView;
							depthAttach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
							depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
							depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
						}

						pso.UpdateRenderInfo(
							{
								opaque.Width(),
								opaque.Height()
							},
							{ opaqueAttach, depthAttach });

						const auto& sceneData = ctx.scene->GetSceneData();
						const auto& projUnjittered = ctx.scene->GetCurrentProjUnjittered();

						glm::mat4 proj{};
						if (ctx.frameState->bHasVisibles &&
							ctx.profiler->debugToggles.aaMode == static_cast<uint32_t>(RD::AntiAliasingMethod::AA_TAA))
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
					})

				.DisableCulling()
				.ForceExecution()

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::Skybox,
							pass.passName);

						auto& pso = std::get<GraphicsScope>(pass.scope);

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& pipeline = pass.pipelines[PIPE_ID_MAIN];

						ctx.profiler->AddDirect(1, TrianglesFromNonIndexed(pipeline.topology, 3));

						pso.BeginRendering(cmd);

						pso.DrawTriangle(cmd, pipeline);

						pso.EndRendering(cmd);
					});
		});
}
