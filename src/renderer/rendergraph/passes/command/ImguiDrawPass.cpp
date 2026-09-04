#include "pch.h"

#include "../../RenderPasses.h"
#include "../../../backend/ImageUtils.h"
#include "../../RenderGraph.h"
#include "../../RenderGraphResources.h"
#include "../../../backend/memory/BindlessImageTable.h"
#include "../../../backend/Swapchain.h"
#include "../../../../profiler/Profiler.h"
#include "../../../frame/FrameContext.h"
#include "../../../backend/BufferBarriers.h"

namespace I = ImageUtils;
namespace B = BufferBarriers;

void RegisterImguiDrawPass(RenderGraph& graph)
{
	graph.AddPass(
		"Imgui_Draw",
		{},
		[&](RenderPassBuilder& builder)
		{
			builder
				.SetPhase(RenderPhase::Present)

				.SetExecutionCondition(
					[](const RenderPassExecutionContext& ctx)
					{
						return ctx.frameState->DrawImgui() && ImGui::GetDrawData() != nullptr;
					})

				.SetRecord(
					[](RenderPassExecutionContext& ctx, RenderPassDesc& pass)
					{
						VkCommandBuffer cmd = ctx.commandBuffer;

						if (ctx.profiler->debugToggles.enableProfilerView)
						{
							const auto& statsBuf = ctx.frameCtx->GetGPUBuffer(RD::Renderer_Buffer::DrawStats);
							const auto& statsReadbackBuf = ctx.frameCtx->GetStatsReadbackBuffer();

							B::ComputeWriteToTransferRead(cmd, statsBuf);

							VkBufferCopy region{ 0, 0, sizeof(GPUStats) };
							vkCmdCopyBuffer(cmd, statsBuf.m_buffer, statsReadbackBuf.m_buffer, 1, &region);
						}

						const auto& swapchain = ctx.swapchain;

						auto swapImage = swapchain->GetCurrentImage();
						auto swapView = swapchain->GetCurrentView();
						const auto swapExtent = swapchain->GetExtent();

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
						renderingInfo.renderArea = { {0, 0}, swapExtent };
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
