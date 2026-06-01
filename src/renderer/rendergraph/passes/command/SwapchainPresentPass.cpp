#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../backend/ImageUtils.h"
#include "EngineTypes.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../../profiler/Profiler.h"

namespace I = ImageUtils;

void RegisterSwapchainPresentPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Swapchain_Present",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.ReadResource(RD::Renderer_RenderTarget::PostNonAAComposite,
					RD::ImageAccess::Read)
				.ReadResource(RD::Renderer_RenderTarget::AAColor,
					RD::ImageAccess::Read)
				.ReadResource(RD::Renderer_RenderTarget::Tonemap,
					RD::ImageAccess::Read)

				.DisableCulling()
				.ForceExecution()

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& postNonAAComposite = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::PostNonAAComposite);
						const auto& aaColor = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::AAColor);
						const auto& tonemap = ctx.imageTable->GetRenderTarget(RD::Renderer_RenderTarget::Tonemap);
						const auto& swapchain = ctx.swapchain;

						AllocatedImage srcImage;

						if (ctx.profiler->debugToggles.enableChromaticAberration)
						{
							srcImage = postNonAAComposite;
						}
						else
						{
							srcImage = ctx.frameState->bCopyPostAAImage ? aaColor : tonemap;
						}

						I::SwapchainPresentCopy(cmd, *swapchain, srcImage);
					});
		});
}
