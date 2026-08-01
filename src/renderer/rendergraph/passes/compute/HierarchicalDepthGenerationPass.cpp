#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../rendergraph/RenderGraphBuilder.h"
#include "../../scopes/ComputeScope.h"
#include "../../../backend/ImageUtils.h"
#include "EngineTypes.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"

namespace I = ImageUtils;

constexpr size_t PIPE_ID_HI_Z = 0;

struct alignas(16) DepthPyramidPush
{
	uint32_t mipLevel;
	float pad0;
	glm::vec2 invSize;
};

void RegisterHiZGenerationPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Hi_Z_Generation",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::Prepass)
				.ForceExecution()

				.ReadResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)

				.InternalResource(
					RD::Renderer_RenderTarget::HiZ,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read,
					0,
					VK_REMAINING_MIP_LEVELS)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						const auto& drawExtent = graph.GetDrawExtent();
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::HiZGeneration,
							pass.passName);

						pass.scope = ComputeScope{ drawExtent , WORKGROUP_8x8 };
						auto& pso = std::get<ComputeScope>(pass.scope);

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& hiZ = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HiZ);
						const auto& dummyUint8 = ctx.imageTable->GetStaticTexture(RD::Renderer_Texture::DummyU8);
						const auto& depth = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto nearestSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);
						const auto hiZSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::HiZ);

						//----------------------------------------
						// MIP GENERATION LOOP
						//----------------------------------------
						Extents2D srcExtent = { hiZ.Width(), hiZ.Height() };
						Extents2D dstExtent = { hiZ.Width(), hiZ.Height() };

						for (uint32_t mip = 0; mip < hiZ.m_mipLevels; ++mip)
						{
							pso.BindReadImage(
								pass.pushWriter,
								RD::PUSH_BINDING_READ_1,
								depth,
								nearestSampler,
								UINT32_MAX,
								RD::ImageAccess::DepthRead);

							if (mip > 0)
							{
								pso.BindReadImage(
									pass.pushWriter,
									RD::PUSH_BINDING_READ_2,
									hiZ,
									hiZSampler,
									mip - 1);
							}
							else
							{
								pso.BindReadImage(
									pass.pushWriter,
									RD::PUSH_BINDING_READ_2,
									dummyUint8,
									hiZSampler);
							}

							pso.BindWriteImage(
								pass.pushWriter,
								RD::PUSH_BINDING_WRITE_1,
								hiZ,
								mip);

							DepthPyramidPush push{};
							push.mipLevel = mip;
							push.invSize =
							{
								1.0f / float(srcExtent.Width()),
								1.0f / float(srcExtent.Height())
							};
							pso.SetPush(push);

							pso.UpdateExtent(dstExtent);

							pso.DispatchComputePass(
								cmd,
								pass.pipelines[PIPE_ID_HI_Z],
								pass.pushWriter);

							I::TransitionLayout(
								cmd,
								hiZ,
								RD::ImageAccess::Write,
								RD::ImageAccess::Read,
								mip,
								1);

							srcExtent = dstExtent;
							dstExtent.Width() = std::max(1u, dstExtent.Width() >> 1);
							dstExtent.Height() = std::max(1u, dstExtent.Height() >> 1);
						}
					});
		});
}

void RegisterHiZGenerationLatePass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Hi_Z_Generation_Late",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::Prepass)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->InstancesActive() &&
							ctx.frameState->IsMeshShaderPath();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::DepthRead)

				.InternalResource(RD::Renderer_RenderTarget::HiZ,
					RD::ImageAccess::Write,
					RD::ImageAccess::Read,
					0,
					VK_REMAINING_MIP_LEVELS)

				.SetRecord(
					[&graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						const auto& drawExtent = graph.GetDrawExtent();
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::HiZGenerationLate,
							pass.passName);

						pass.scope = ComputeScope{ drawExtent , WORKGROUP_8x8 };
						auto& pso = std::get<ComputeScope>(pass.scope);

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& hiZ = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HiZ);
						const auto& dummyUint8 = ctx.imageTable->GetStaticTexture(RD::Renderer_Texture::DummyU8);
						const auto& depth = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
						const auto nearestSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::NearestClamp);
						const auto hiZSampler = ctx.imageTable->GetSampler(RD::Renderer_Sampler::HiZ);

						//----------------------------------------
						// MIP GENERATION LOOP
						//----------------------------------------
						Extents2D srcExtent = { hiZ.Width(), hiZ.Height() };
						Extents2D dstExtent = { hiZ.Width(), hiZ.Height() };

						for (uint32_t mip = 0; mip < hiZ.m_mipLevels; ++mip)
						{
							pso.BindReadImage(
								pass.pushWriter,
								RD::PUSH_BINDING_READ_1,
								depth,
								nearestSampler,
								UINT32_MAX,
								RD::ImageAccess::DepthRead);

							if (mip > 0)
							{
								pso.BindReadImage(
									pass.pushWriter,
									RD::PUSH_BINDING_READ_2,
									hiZ,
									hiZSampler,
									mip - 1);
							}
							else
							{
								pso.BindReadImage(
									pass.pushWriter,
									RD::PUSH_BINDING_READ_2,
									dummyUint8,
									hiZSampler);
							}

							pso.BindWriteImage(
								pass.pushWriter,
								RD::PUSH_BINDING_WRITE_1,
								hiZ,
								mip);

							DepthPyramidPush push{};
							push.mipLevel = mip;
							push.invSize =
							{
								1.0f / float(srcExtent.Width()),
								1.0f / float(srcExtent.Height())
							};
							pso.SetPush(push);

							pso.UpdateExtent(dstExtent);

							pso.DispatchComputePass(
								cmd,
								pass.pipelines[PIPE_ID_HI_Z],
								pass.pushWriter);

							I::TransitionLayout(
								cmd,
								hiZ,
								RD::ImageAccess::Write,
								RD::ImageAccess::Read,
								mip,
								1);

							srcExtent = dstExtent;
							dstExtent.Width() = std::max(1u, dstExtent.Width() >> 1);
							dstExtent.Height() = std::max(1u, dstExtent.Height() >> 1);
						}
					});
		});
}

