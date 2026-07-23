#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"

static constexpr size_t PIPE_ID_TILE_SHADE = 0;

void RegisterOpaqueTileShadingPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Opaque_Tile_Shading",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->IsVisibilityDeferred() &&
							ctx.frameState->InstancesActive() &&
							!ctx.frameState->IsWireframeOn();
					})

				.WriteResource(
					RD::Renderer_RenderTarget::Opaque,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read)

				.SetSetup(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						const auto& drawExtent = graph.GetDrawExtent();
						pass.scope = ComputeScope{{ drawExtent }};
						auto& pso = std::get<ComputeScope>(pass.scope);

						pso.SetPush(ctx.profiler->forwardPush);

						const auto& albedoRough = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::MaterialAlbedoRough);
						const auto& normal = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::MaterialNormal);
						const auto& metal = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::MaterialMetal);
						const auto& emissive = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::MaterialEmissive);
						const auto& depthResolved = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& vsNormals = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::ViewSpaceNormals);
						const auto& opaque = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Opaque);
						const auto& aoRaw = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::AORaw);
						const auto& bentNormals = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::BentNormals);
						const auto& contactShadows = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::SSContactShadows);
						const auto linearClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::LinearClamp);
						const auto nearestClampSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);

						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, albedoRough, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, normal, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_3, metal, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_4, emissive, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_5, depthResolved, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_6, vsNormals , nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_7, aoRaw, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_8, contactShadows, nearestClampSampler);
						pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_9, bentNormals, linearClampSampler);

						pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, opaque);
					})

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::OpaqueTileShading,
							pass.passName);

						auto& pso = std::get<ComputeScope>(pass.scope);
						pso.DispatchComputePass(
							ctx.commandBuffer,
							pass.pipelines[PIPE_ID_TILE_SHADE],
							pass.pushWriter);
					});
		});
}
