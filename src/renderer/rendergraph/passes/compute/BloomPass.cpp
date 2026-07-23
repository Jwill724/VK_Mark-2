#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../../backend/ImageUtils.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"

namespace I = ImageUtils;

static constexpr size_t PIPE_ID_BLOOM_DOWNSAMPLE = 0;
static constexpr size_t PIPE_ID_BLOOM_UPSAMPLE   = 1;

void RegisterBloomPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Bloom",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.profiler->debugToggles.enableBloom &&
							ctx.frameState->InstancesActive() &&
							!ctx.frameState->DebugRendering();
					})

				.SetSetup(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						const auto& drawExtent = graph.GetDrawExtent();
						pass.scope = ComputeScope{ drawExtent, WORKGROUP_8x8 };
					})

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::Bloom,
							pass.passName);

						auto& pso = std::get<ComputeScope>(pass.scope);
						pso.SetPush(ctx.profiler->bloomPush);

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& bloom = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::BloomMipchain);
						const auto& depth = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& sceneHDR = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Opaque);
						const auto linearClamp = ctx.imageTable->GetSampler(RD::Renderer_Sampler::LinearClamp);
						const auto nearestClamp = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);

						const uint32_t mips = bloom.m_mipLevels;

						//================================================================
						// DOWNSAMPLE  (sceneHDR -> mip0 w/ Karis, then mip[i-1] -> mip[i])
						//================================================================
						ImageUtils::TransitionLayout(cmd, bloom, RD::ImageAccess::Read, RD::ImageAccess::Write, 0, mips);

						Extents2D dstExtent = { bloom.Width(), bloom.Height() };   // half draw extent

						for (uint32_t mip = 0; mip < mips; ++mip)
						{
							Extents2D srcExtent;
							if (mip == 0)
							{
								pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, sceneHDR, linearClamp);
								pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, depth, nearestClamp);
								srcExtent = { sceneHDR.Width(), sceneHDR.Height() };
							}
							else
							{
								pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, bloom, linearClamp, mip - 1);
								pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, depth, nearestClamp); // Means nothing
								srcExtent = { std::max(1u, bloom.Width()  >> (mip - 1)),
											  std::max(1u, bloom.Height() >> (mip - 1)) };
							}

							pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, bloom, mip);

							pso.EditPush<BloomPush>([srcExtent, dstExtent, mip](BloomPush& push) {
								push.srcTexelSize = { 1.0f / float(srcExtent.Width()), 1.0f / float(srcExtent.Height()) };
								push.dstRes       = { dstExtent.Width(), dstExtent.Height() };
								push.flags        = (mip == 0) ? 1u : 0u;
							});

							pso.UpdateExtent(dstExtent);
							pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_BLOOM_DOWNSAMPLE], pass.pushWriter);

							ImageUtils::TransitionLayout(cmd, bloom, RD::ImageAccess::Write, RD::ImageAccess::Read, mip, 1);

							dstExtent.Width()  = std::max(1u, dstExtent.Width()  >> 1);
							dstExtent.Height() = std::max(1u, dstExtent.Height() >> 1);
						}

						//================================================================
						// UPSAMPLE  (mip[i+1] tent + mip[i] -> mip[i],  coarse -> fine)
						//================================================================
						for (int i = int(mips) - 2; i >= 0; --i)
						{
							// derive from base each step
							Extents2D dstExtent = { std::max(1u, bloom.Width()  >> uint32_t(i)),
													std::max(1u, bloom.Height() >> uint32_t(i)) };
							Extents2D srcExtent = { std::max(1u, bloom.Width()  >> uint32_t(i + 1)),
													std::max(1u, bloom.Height() >> uint32_t(i + 1)) };

							ImageUtils::TransitionLayout(cmd, bloom, RD::ImageAccess::Read, RD::ImageAccess::Write, i, 1);

							pso.BindReadImage (pass.pushWriter, RD::PUSH_BINDING_READ_1,  bloom, linearClamp, i + 1); // tent src
							pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, bloom, i);                  // read+add+write

							pso.EditPush<BloomPush>([srcExtent, dstExtent](BloomPush& push) {
								push.srcTexelSize = { 1.0f / float(srcExtent.Width()), 1.0f / float(srcExtent.Height()) };
								push.dstRes       = { dstExtent.Width(), dstExtent.Height() };
							});

							pso.UpdateExtent(dstExtent);
							pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_BLOOM_UPSAMPLE], pass.pushWriter);

							ImageUtils::TransitionLayout(cmd, bloom, RD::ImageAccess::Write, RD::ImageAccess::Read, i, 1);
						}
					});
		});
}
