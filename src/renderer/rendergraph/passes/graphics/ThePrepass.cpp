#include "pch.h"

#include "../../RenderPasses.h"
#include "../../scopes/GraphicsScope.h"
#include "../../RenderGraph.h"
#include "../../../backend/pipelines/PipelineBundles.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"
#include "../../../frame/FrameContext.h"

struct PrepassVariant
{
	uint32_t                    slot;
	BasePrepassPipelineSlot     pipe;
};

static constexpr PrepassVariant kPrepassVariants[] =
{
	{ RD::VIS_SLOT_OPAQUE,
	  BasePrepassPipelineSlot::PrepassMesh },
	{ RD::VIS_SLOT_OPAQUE_MASKED,
	  BasePrepassPipelineSlot::PrepassMaskedMesh },
};

// Shared attachment setup — phase 2 flips the load ops
static void BuildPrepassAttachments(
	RenderPassExecutionContext& ctx,
	GraphicsScope& pso,
	bool bLoadExisting)
{
	const auto& depthResolved    = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::DepthResolved);
	const auto& visibility       = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Visibility);
	const auto& viewSpaceNormals = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::ViewNormals);

	AttachmentDesc prepassDepth{};
	prepassDepth.imageView   = depthResolved.m_imageView;
	prepassDepth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	prepassDepth.SetDepth(0);

	AttachmentDesc prepassVisibility{};
	prepassVisibility.imageView   = visibility.m_imageView;
	prepassVisibility.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	uint32_t resetIndices[4] = { RD::INVALID_U32, RD::INVALID_U32, 0u, 0u };
	prepassVisibility.SetColorU32(resetIndices);

	AttachmentDesc prepassNormal{};
	prepassNormal.imageView   = viewSpaceNormals.m_imageView;
	prepassNormal.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	prepassNormal.SetColor({ 0.5f, 0.5f, 0.0f, 0.0f });

	if (bLoadExisting)
	{
		// Phase 2 accumulates onto phase 1's results
		prepassDepth.loadOp      = VK_ATTACHMENT_LOAD_OP_LOAD;
		prepassVisibility.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		prepassNormal.loadOp     = VK_ATTACHMENT_LOAD_OP_LOAD;
	}

	pso.UpdateRenderInfo(
		{ depthResolved.Width(), depthResolved.Height() },
		{ prepassVisibility, prepassNormal, prepassDepth });
}

static void RecordPrepassDraw(
	RenderPassExecutionContext& ctx,
	RenderPassDesc& pass,
	GraphicsScope& pso,
	uint32_t phase)
{
	VkCommandBuffer cmd  = ctx.commandBuffer;
	const auto& frameCtx = ctx.frameCtx;

	const auto taskBuffer  = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::TaskDispatch).m_buffer;
	const auto countBuffer = frameCtx->GetGPUBuffer(RD::Renderer_Buffer::IndirectDrawCounts).m_buffer;

	const auto& hiZ = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::HiZ);

	for (const PrepassVariant& variant : kPrepassVariants)
	{
		PrepassTaskPush push{};
		push.slot = variant.slot;
		push.phase = phase;
		pso.SetPush(push);

		pso.BindReadImage(
			pass.pushWriter,
			RD::PUSH_BINDING_READ_1,
			hiZ,
			ctx.imageTable->GetSampler(RD::Renderer_Sampler::HiZ));

		pso.DrawMeshTasksIndirectCount(
			cmd, variant.slot,
			taskBuffer, countBuffer,
			pass.pipelines[static_cast<size_t>(variant.pipe)],
			pass.pushWriter);
	}
}

void RegisterThePrepass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Prepass",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::Prepass)

				.ForceExecution()

				.ReadResource(
					RD::Renderer_RenderTarget::HiZ,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::GraphicsDepthWrite,
					RD::ImageAccess::DepthRead)

				.WriteResource(
					RD::Renderer_RenderTarget::Visibility,
					RD::ImageAccess::GraphicsColorWrite,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::ViewNormals,
					RD::ImageAccess::GraphicsColorWrite,
					RD::ImageAccess::Read)

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::Prepass,
							pass.passName);

						pass.scope = GraphicsScope{};
						auto& pso = std::get<GraphicsScope>(pass.scope);

						BuildPrepassAttachments(ctx, pso, /*bLoadExisting*/ false);

						pso.BeginRendering(ctx.commandBuffer);
						RecordPrepassDraw(ctx, pass, pso, /*phase*/ 0u);
						pso.EndRendering(ctx.commandBuffer);
					});
		});
}

void RegisterThePrepassLate(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Prepass_Late",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::Prepass)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return
							ctx.frameState->InstancesActive();
					})

				.ReadResource(
					RD::Renderer_RenderTarget::HiZ,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::DepthResolved,
					RD::ImageAccess::GraphicsDepthWrite,
					RD::ImageAccess::DepthRead)

				.WriteResource(
					RD::Renderer_RenderTarget::Visibility,
					RD::ImageAccess::GraphicsColorWrite,
					RD::ImageAccess::Read)

				.WriteResource(
					RD::Renderer_RenderTarget::ViewNormals,
					RD::ImageAccess::GraphicsColorWrite,
					RD::ImageAccess::Read)

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						auto passScope = ctx.profiler->ProfilePass(
							*ctx.frameCtx,
							ctx.commandBuffer,
							RD::Renderer_Pass::PrepassLate,
							pass.passName);

						pass.scope = GraphicsScope{};
						auto& pso = std::get<GraphicsScope>(pass.scope);

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& frameCtx = ctx.frameCtx;

						{
							VkMemoryBarrier2 mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
							mb.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
							                   VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
							mb.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
							                   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
							mb.dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT |
							                   VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
							mb.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT |
							                   VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT  |
							                   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
							                   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

							VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
							di.memoryBarrierCount = 1;
							di.pMemoryBarriers    = &mb;
							vkCmdPipelineBarrier2(cmd, &di);
						}

						BuildPrepassAttachments(ctx, pso, /*bLoadExisting*/ true);

						pso.BeginRendering(cmd);
						RecordPrepassDraw(ctx, pass, pso, /*phase*/ 1u);
						pso.EndRendering(cmd);
					});
		});
}
