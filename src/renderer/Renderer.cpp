#include "pch.h"

#include "Renderer.h"
#include "profiler/EditorImgui.h"
#include "vulkan/Backend.h"
#include "RenderScene.h"
#include "SceneGraph.h"

#include "utils/RendererUtils.h"
#include "core/AssetManager.h"

#include "gpu/PipelineManager.h"
#include "gpu/CommandBuffer.h"

#include "common/ResourceTypes.h"


namespace Renderer {
	std::array<VkImageView, 3> _renderImgViews;

	TimelineSync _transferSync;

	void colorCorrectPass(FrameContext& frame);
	void geometryPass(std::array<VkImageView, 3> imageViews, FrameContext& frameCtx);
}

void Renderer::initFrameContexts(VkDevice device, uint32_t graphicsIndex, uint32_t transferIndex,
	VkExtent2D drawExtent, VkDescriptorSetLayout layout, const VmaAllocator allocator) {
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		auto& frame = _frameContexts[i];
		frame.syncObjs.swapchainSemaphore = RendererUtils::createSemaphore();
		frame.syncObjs.semaphore = RendererUtils::createSemaphore();
		frame.syncObjs.fence = RendererUtils::createFence();
		frame.graphicsPool = CommandBuffer::createCommandPool(device, graphicsIndex);
		frame.transferPool = CommandBuffer::createCommandPool(device, transferIndex);
		frame.commandBuffer = CommandBuffer::createCommandBuffer(device, frame.graphicsPool);
		frame.set = DescriptorSetOverwatch::mainDescriptorManager.allocateDescriptor(device, layout);
		frame.addressTableBuffer = BufferUtils::createBuffer(
			sizeof(GPUAddressTable),
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT |
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VMA_MEMORY_USAGE_GPU_ONLY,
			allocator);
		frame.addressTableStaging = BufferUtils::createBuffer(
			sizeof(GPUAddressTable),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VMA_MEMORY_USAGE_CPU_ONLY,
			allocator);
		assert(frame.addressTableStaging.info.pMappedData);
		frame.frameIndex = i;
		frame.ready = true;
	}

	_drawExtent = { drawExtent.width, drawExtent.height, 1 };

	RendererUtils::createTimelineSemaphore(_transferSync);
}

void Renderer::prepareFrameContext(FrameContext& frameCtx) {
	auto device = Backend::getDevice();
	auto swapchain = Backend::getSwapchainDef().swapchain;

	if (frameCtx.transferFence != VK_NULL_HANDLE) {
		VK_CHECK(vkWaitForFences(device, 1, &frameCtx.transferFence, VK_TRUE, UINT64_MAX));
		VK_CHECK(vkResetFences(device, 1, &frameCtx.transferFence));
		frameCtx.transferFence = VK_NULL_HANDLE;
	}

	VK_CHECK(vkWaitForFences(device, 1, &frameCtx.syncObjs.fence, VK_TRUE, UINT64_MAX));
	VK_CHECK(vkResetFences(device, 1, &frameCtx.syncObjs.fence));

	if (!frameCtx.transferCmds.empty()) {
		vkFreeCommandBuffers(device, frameCtx.transferPool, static_cast<uint32_t>(frameCtx.transferCmds.size()), frameCtx.transferCmds.data());
		frameCtx.transferCmds.clear();
	}

	frameCtx.deletionQueue.flush();

	uint32_t imageIndex;
	VkResult swpResult = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, frameCtx.syncObjs.swapchainSemaphore, VK_NULL_HANDLE, &imageIndex);
	if (swpResult == VK_ERROR_OUT_OF_DATE_KHR || swpResult == VK_SUBOPTIMAL_KHR) {
		Backend::getGraphicsQueue().waitIdle();
		Backend::resizeSwapchain();
		return;
	}
	assert(swpResult == VK_SUCCESS && "Failed to present swap chain image!");
	frameCtx.swapchainImageIndex = imageIndex;
	frameCtx.swapchainResult = swpResult;

	VK_CHECK(vkResetCommandBuffer(frameCtx.commandBuffer, 0));
}

void Renderer::submitFrame(FrameContext& frameCtx, GPUResources& resources) {
	fmt::print("---- SUBMIT FRAME [{}] ----\n", _frameNumber);
	fmt::print("CommandBuffer: {}\n", static_cast<void*>(frameCtx.commandBuffer));
	fmt::print("Fence: {}\n", static_cast<void*>(frameCtx.syncObjs.fence));
	fmt::print("WaitSemaphore: {} | SignalSemaphore: {}\n",
		static_cast<void*>(frameCtx.syncObjs.swapchainSemaphore),
		static_cast<void*>(frameCtx.syncObjs.semaphore));
	fmt::print("Swapchain Image Index: {}\n", frameCtx.swapchainImageIndex);


	VkCommandBufferSubmitInfo cmdInfo{};
	cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	cmdInfo.pNext = nullptr;
	cmdInfo.commandBuffer = frameCtx.commandBuffer;
	cmdInfo.deviceMask = 0;

	// --- Wait: Swapchain Acquire ---
	VkSemaphoreSubmitInfo waitSwapchain{};
	waitSwapchain.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	waitSwapchain.semaphore = frameCtx.syncObjs.swapchainSemaphore;
	waitSwapchain.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR;
	waitSwapchain.deviceIndex = 0;
	waitSwapchain.value = 1;

	// --- Wait: Transfer Completion ---
	VkSemaphoreSubmitInfo waitTransfer{};
	if (!frameCtx.transferCmds.empty()) {
		assert(_transferSync.semaphore != VK_NULL_HANDLE && "Timeline semaphore must be set before rendering");
		waitTransfer.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		waitTransfer.semaphore = _transferSync.semaphore;
		waitTransfer.stageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
		waitTransfer.deviceIndex = 0;
		assert(frameCtx.transferWaitValue <= _transferSync.signalValue - 1);
		waitTransfer.value = frameCtx.transferWaitValue;
	}

	// --- Signal: Rendering Done (for Present) ---
	VkSemaphoreSubmitInfo signalRender{};
	signalRender.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	signalRender.semaphore = frameCtx.syncObjs.semaphore;
	signalRender.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
	signalRender.deviceIndex = 0;
	signalRender.value = 1;

	std::vector<VkSemaphoreSubmitInfo> waitInfos;
	if (waitSwapchain.semaphore != VK_NULL_HANDLE) waitInfos.push_back(waitSwapchain);
	if (waitTransfer.semaphore != VK_NULL_HANDLE) waitInfos.push_back(waitTransfer);


	VkSubmitInfo2 graphicsSubmitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
	graphicsSubmitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitInfos.size());
	graphicsSubmitInfo.pWaitSemaphoreInfos = waitInfos.data();
	graphicsSubmitInfo.signalSemaphoreInfoCount = 1;
	graphicsSubmitInfo.pSignalSemaphoreInfos = &signalRender;
	graphicsSubmitInfo.commandBufferInfoCount = 1;
	graphicsSubmitInfo.pCommandBufferInfos = &cmdInfo;


	auto& graphicsQueue = Backend::getGraphicsQueue();
	auto& presentQueue = Backend::getPresentQueue();

	//vkDeviceWaitIdle(Backend::getDevice());
	//fmt::print("Submitting to Graphics Queue...\n");
	VK_CHECK(vkQueueSubmit2(graphicsQueue.queue, 1, &graphicsSubmitInfo, frameCtx.syncObjs.fence));
	//fmt::print("Submission complete.\n");

	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &frameCtx.syncObjs.semaphore;

	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &Backend::getSwapchainDef().swapchain;
	presentInfo.pImageIndices = &frameCtx.swapchainImageIndex;


	//fmt::print("Presenting frame...\n");
	frameCtx.swapchainResult = vkQueuePresentKHR(presentQueue.queue, &presentInfo);
	//fmt::print("Present result: {}\n", static_cast<int32_t>(frameCtx.swapchainResult));

	if (frameCtx.swapchainResult == VK_ERROR_OUT_OF_DATE_KHR ||
		frameCtx.swapchainResult == VK_SUBOPTIMAL_KHR
		|| Engine::windowModMode().windowResized)
	{
		if (graphicsQueue.queue != presentQueue.queue) {
			presentQueue.waitIdle();
		}
		else {
			graphicsQueue.waitIdle();
		}

		Backend::resizeSwapchain();
	}
	else {
		assert(frameCtx.swapchainResult == VK_SUCCESS && "Failed to present swap chain image!");
	}

	auto instBuf = frameCtx.instanceBuffer;
	auto indirectBuf = frameCtx.indirectCmdBuffer;
	if (instBuf.buffer != VK_NULL_HANDLE && indirectBuf.buffer != VK_NULL_HANDLE) {
		auto allocator = resources.getAllocator();
		frameCtx.deletionQueue.push_function([instBuf, indirectBuf, allocator]() mutable {
			BufferUtils::destroyBuffer(instBuf, allocator);
			BufferUtils::destroyBuffer(indirectBuf, allocator);
		});
	}
	// double buffering, frames indexed at 0 and 1
	_frameNumber++;
}

void Renderer::recordRenderCommand(FrameContext& frameCtx) {

	VkCommandBufferBeginInfo cmdBeginInfo{};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	auto& swp = Backend::getSwapchainDef();
	auto& draw = ResourceManager::getDrawImage();
	auto& msaa = ResourceManager::getMSAAImage();
	auto& depth = ResourceManager::getDepthImage();
	auto& postProcess = ResourceManager::getPostProcessImage();

	_drawExtent.height = static_cast<uint32_t>(std::min(swp.extent.height, draw.imageExtent.height));
	_drawExtent.width = static_cast<uint32_t>(std::min(swp.extent.width, draw.imageExtent.width));

	VkDescriptorSet sets[] = {
		DescriptorSetOverwatch::getUnifiedDescriptors().descriptorSet,
		frameCtx.set
	};

	VK_CHECK(vkBeginCommandBuffer(frameCtx.commandBuffer, &cmdBeginInfo));

	vkCmdBindDescriptorSets(frameCtx.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		Pipelines::_globalLayout.layout, 0, 2, sets, 0, nullptr);

	vkCmdBindDescriptorSets(frameCtx.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		Pipelines::_globalLayout.layout, 0, 2, sets, 0, nullptr);

	// color, depth and msaa transitions
	RendererUtils::transitionImage(frameCtx.commandBuffer,
		draw.image,
		draw.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	if (MSAACOUNT != 1u) {
		RendererUtils::transitionImage(frameCtx.commandBuffer,
			msaa.image,
			msaa.imageFormat,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	}
	RendererUtils::transitionImage(frameCtx.commandBuffer,
		depth.image,
		depth.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	_renderImgViews = { draw.imageView, msaa.imageView, depth.imageView };

	geometryPass(_renderImgViews, frameCtx);

	// Post process transition and copy
	RendererUtils::transitionImage(frameCtx.commandBuffer,
		draw.image,
		draw.imageFormat,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	RendererUtils::transitionImage(frameCtx.commandBuffer,
		postProcess.image,
		postProcess.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	RendererUtils::copyImageToImage(frameCtx.commandBuffer,
		draw.image,
		postProcess.image,
		{ _drawExtent.width, _drawExtent.height },
		{ _drawExtent.width, _drawExtent.height }
	);

	RendererUtils::transitionImage(frameCtx.commandBuffer,
		draw.image,
		draw.imageFormat,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_IMAGE_LAYOUT_GENERAL);
	RendererUtils::transitionImage(frameCtx.commandBuffer,
		postProcess.image,
		postProcess.imageFormat,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_GENERAL);

	colorCorrectPass(frameCtx);

	RendererUtils::transitionImage(frameCtx.commandBuffer,
		postProcess.image,
		postProcess.imageFormat,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	RendererUtils::transitionImage(frameCtx.commandBuffer,
		swp.images[frameCtx.swapchainImageIndex],
		draw.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	RendererUtils::copyImageToImage(frameCtx.commandBuffer,
		postProcess.image,
		swp.images[frameCtx.swapchainImageIndex],
		{ _drawExtent.width, _drawExtent.height },
		swp.extent
	);

	// Transition swapchain to COLOR_ATTACHMENT_OPTIMAL for ImGui
	RendererUtils::transitionImage(frameCtx.commandBuffer,
		swp.images[frameCtx.swapchainImageIndex],
		draw.imageFormat,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	EditorImgui::drawImgui(frameCtx.commandBuffer, swp.imageViews[frameCtx.swapchainImageIndex], false);

	RendererUtils::transitionImage(frameCtx.commandBuffer,
		swp.images[frameCtx.swapchainImageIndex],
		draw.imageFormat,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	VK_CHECK(vkEndCommandBuffer(frameCtx.commandBuffer));
}

void Renderer::geometryPass(std::array<VkImageView, 3> imageViews, FrameContext& frameCtx) {
	VkRenderingAttachmentInfo colorAttachment = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.pNext = nullptr,
		.imageView = (MSAACOUNT == 1u) ? imageViews[0] : imageViews[1],
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.resolveMode = (MSAACOUNT == 1u) ? VK_RESOLVE_MODE_NONE : VK_RESOLVE_MODE_AVERAGE_BIT,
		.resolveImageView = (MSAACOUNT == 1u) ? VK_NULL_HANDLE : imageViews[0],
		.resolveImageLayout = (MSAACOUNT == 1u) ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE
	};

	VkRenderingAttachmentInfo depthAttachment = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.pNext = nullptr,
		.imageView = imageViews[2],
		.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		.resolveMode = VK_RESOLVE_MODE_NONE,
		.resolveImageView = VK_NULL_HANDLE,
		.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE
	};
	depthAttachment.clearValue.depthStencil.depth = 1.f;

	VkRenderingInfo renderInfo = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
		.pNext = nullptr,
		.flags = 0,
		.renderArea = { { 0, 0 }, { _drawExtent.width, _drawExtent.height } },
		.layerCount = 1,
		.viewMask = 0,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAttachment,
		.pDepthAttachment = &depthAttachment,
		.pStencilAttachment = nullptr
	};

	if (_transferSync.semaphore != VK_NULL_HANDLE) {
		if (frameCtx.instanceBuffer.buffer != VK_NULL_HANDLE && frameCtx.indirectCmdBuffer.buffer != VK_NULL_HANDLE) {
			RendererUtils::insertTransferToGraphicsBufferBarrier(frameCtx.commandBuffer, frameCtx.instanceBuffer.buffer);
			RendererUtils::insertTransferToGraphicsBufferBarrier(frameCtx.commandBuffer, frameCtx.indirectCmdBuffer.buffer);
			RendererUtils::insertTransferToGraphicsBufferBarrier(frameCtx.commandBuffer, frameCtx.addressTableBuffer.buffer);
		}
	}

	vkCmdBeginRendering(frameCtx.commandBuffer, &renderInfo);
	RenderScene::renderGeometry(frameCtx);
	vkCmdEndRendering(frameCtx.commandBuffer);
}

void Renderer::colorCorrectPass(FrameContext& frame) {
	auto& pipeline = Pipelines::postProcessPipeline;
	auto& compEffect = pipeline.getComputeEffect();

	vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compEffect.pipeline);

	vkCmdPushConstants(frame.commandBuffer,
		Pipelines::_globalLayout.layout,
		Pipelines::_globalLayout.pcRange.stageFlags,
		Pipelines::_globalLayout.pcRange.offset,
		Pipelines::_globalLayout.pcRange.size,
		&compEffect.pushData);

	const auto xDispatch = static_cast<uint32_t>(std::ceil(_drawExtent.width / 16.0f));
	const auto yDispatch = static_cast<uint32_t>(std::ceil(_drawExtent.height / 16.0f));

	vkCmdDispatch(frame.commandBuffer, xDispatch, yDispatch, 1);
}

// cleans up frame contexts
void Renderer::cleanup() {
	auto device = Backend::getDevice();
	auto allocator = Engine::getState().getGPUResources().getAllocator();

	for (auto& frame : _frameContexts) {
		frame.deletionQueue.flush();

		// Sync objects first
		if (frame.syncObjs.fence) {
			vkDestroyFence(device, frame.syncObjs.fence, nullptr);
		}
		if (frame.syncObjs.semaphore) {
			vkDestroySemaphore(device, frame.syncObjs.semaphore, nullptr);
		}
		if (frame.syncObjs.swapchainSemaphore) {
			vkDestroySemaphore(device, frame.syncObjs.swapchainSemaphore, nullptr);
		}
		if (frame.transferFence) {
			vkDestroyFence(device, frame.transferFence, nullptr);
		}

		// Pools 2nd
		if (frame.graphicsPool) {
			vkDestroyCommandPool(device, frame.graphicsPool, nullptr);
		}
		if (frame.transferPool) {
			vkDestroyCommandPool(device, frame.transferPool, nullptr);
		}

		// address buffers last
		if (frame.addressTableBuffer.buffer) {
			BufferUtils::destroyBuffer(frame.addressTableBuffer, allocator);
		}
		if (frame.addressTableStaging.buffer) {
			BufferUtils::destroyBuffer(frame.addressTableStaging, allocator);
		}

		frame.ready = false;
	}

	if (_transferSync.semaphore) {
		vkDestroySemaphore(device, _transferSync.semaphore, nullptr);
	}
}