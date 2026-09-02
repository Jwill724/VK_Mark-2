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

static constexpr uint32_t BLOOM_FIRST_DOWNSAMPLE_BIT = 1u;
static constexpr uint32_t BLOOM_EMISSIVE_BIT         = 2u;

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
				.SetPhase(RenderPhase::PostProcess)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.profiler->debugToggles.enableBloom &&
							ctx.frameState->InstancesActive() &&
							!ctx.frameState->DebugRendering();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::HDRScene,
					RD::ImageAccess::Read)

				.ReadResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)

				.ReadResource(
					RD::Renderer_RenderTarget::GBufferEmissive,
					RD::ImageAccess::Read)

				.InternalResource(
					RD::Renderer_RenderTarget::BloomMipchain,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read,
					0,
					VK_REMAINING_MIP_LEVELS)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::Bloom,
							pass.passName);

						pass.scope = ComputeScope{ graph.GetDisplayExtent(), WORKGROUP_8x8 };
						auto& pso = std::get<ComputeScope>(pass.scope);
						pso.SetPush(ctx.profiler->bloomPush);

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& bloom = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::BloomMipchain);
						const auto& depth = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto& gbEmissive = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::GBufferEmissive);
						const auto& sceneHDR = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HDRScene);
						const auto linearClamp = ctx.imageTable->GetSampler(RD::Renderer_Sampler::LinearClamp);
						const auto nearestClamp = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);

						const bool emissiveEnabled = ctx.profiler->bloomPush.emissiveBoost > 0.0f;

						const uint32_t mips = bloom.m_mipLevels;

						//================================================================
						// DOWNSAMPLE  (sceneHDR -> mip0 w/ Karis, then mip[i-1] -> mip[i])
						//================================================================
						Extents2D dstExtent = { bloom.Width(), bloom.Height() };   // half draw extent

						for (uint32_t mip = 0; mip < mips; ++mip)
						{
							Extents2D srcExtent;
							if (mip == 0)
							{
								pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, sceneHDR, linearClamp);
								pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, depth, nearestClamp, UINT32_MAX, RD::ImageAccess::DepthRead);
								srcExtent = { sceneHDR.Width(), sceneHDR.Height() };
							}
							else
							{
								pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_1, bloom, linearClamp, mip - 1);
								pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_2, depth, nearestClamp, UINT32_MAX, RD::ImageAccess::DepthRead); // Means nothing
								srcExtent = { std::max(1u, bloom.Width()  >> (mip - 1)),
											  std::max(1u, bloom.Height() >> (mip - 1)) };
							}

							pso.BindReadImage(pass.pushWriter, RD::PUSH_BINDING_READ_3, gbEmissive, linearClamp);
							pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, bloom, mip);

							uint32_t flags = 0u;
							if (mip == 0)
							{
								flags |= BLOOM_FIRST_DOWNSAMPLE_BIT;
								if (emissiveEnabled)
									flags |= BLOOM_EMISSIVE_BIT;
							}

							pso.EditPush<BloomPush>([srcExtent, dstExtent, flags](BloomPush& push) {
								push.srcTexelSize = { 1.0f / float(srcExtent.Width()), 1.0f / float(srcExtent.Height()) };
								push.dstRes       = { dstExtent.Width(), dstExtent.Height() };
								push.flags        = flags;
							});

							pso.UpdateExtent(dstExtent);
							pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_BLOOM_DOWNSAMPLE], pass.pushWriter);

							I::TransitionLayout(cmd, bloom, RD::ImageAccess::Write, RD::ImageAccess::Read, mip, 1);

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

							I::TransitionLayout(cmd, bloom, RD::ImageAccess::Read, RD::ImageAccess::Write, i, 1);

							pso.BindReadImage (pass.pushWriter, RD::PUSH_BINDING_READ_1,  bloom, linearClamp, i + 1); // tent src
							pso.BindWriteImage(pass.pushWriter, RD::PUSH_BINDING_WRITE_1, bloom, i);                  // read+add+write

							pso.EditPush<BloomPush>([srcExtent, dstExtent](BloomPush& push) {
								push.srcTexelSize = { 1.0f / float(srcExtent.Width()), 1.0f / float(srcExtent.Height()) };
								push.dstRes       = { dstExtent.Width(), dstExtent.Height() };
							});

							pso.UpdateExtent(dstExtent);
							pso.DispatchComputePass(cmd, pass.pipelines[PIPE_ID_BLOOM_UPSAMPLE], pass.pushWriter);

							I::TransitionLayout(cmd, bloom, RD::ImageAccess::Write, RD::ImageAccess::Read, i, 1);
						}
					});
		});
}
