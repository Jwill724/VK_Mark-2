#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../backend/ImageUtils.h"
#include "EngineTypes.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../backend/Swapchain.h"

namespace I = ImageUtils;

void RegisterImguiDrawPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines)
{
	graph.AddPass(
		"Imgui_Draw",
		pipelines,
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return ctx.frameState->bShowImgui && ImGui::GetDrawData() != nullptr;
					})

				.SetRecord(
					[graph](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						const auto& drawExtent = graph.GetDrawExtent();

						VkCommandBuffer cmd = ctx.commandBuffer;

						const auto& swapchain = ctx.swapchain;

						auto swapImage = swapchain->GetCurrentImage();
						auto swapView = swapchain->GetCurrentView();

						I::TransitionRawImageLayout(
							cmd,
							swapImage,
							ImageAspect::Color,
							RD::ImageAccess::Present,
							RD::ImageAccess::GraphicsColorWrite);

						VkClearValue clearValue { .color = { 0.0f, 0.0f, 0.0f, 1.0f } };

						VkRenderingAttachmentInfo colorAttachment{};
						colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
						colorAttachment.pNext = nullptr;
						colorAttachment.imageView = swapView;
						colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
						colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
						colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

						VkRenderingInfo renderingInfo{};
						renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
						renderingInfo.colorAttachmentCount = 1;
						renderingInfo.pColorAttachments = &colorAttachment;
						renderingInfo.renderArea = { {0, 0}, { drawExtent.Width(), drawExtent.Height() } };
						renderingInfo.layerCount = 1;
						renderingInfo.viewMask = 0;

						vkCmdBeginRendering(cmd, &renderingInfo);
						ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
						vkCmdEndRendering(cmd);

						I::TransitionRawImageLayout(
							cmd,
							swapImage,
							ImageAspect::Color,
							RD::ImageAccess::GraphicsColorWrite,
							RD::ImageAccess::Present);
					});
		});
}
