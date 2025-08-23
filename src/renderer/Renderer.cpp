#include "pch.h"

#include "Renderer.h"
#include "engine/platform/profiler/EditorImgui.h"
#include "scene/RenderScene.h"
#include "utils/BufferUtils.h"
#include "core/AssetManager.h"
#include "utils/SyncUtils.h"

namespace Renderer {
	VkExtent3D _drawExtent;
	std::mutex _drawExtentMutex;

	const VkExtent3D getDrawExtent() {
		std::scoped_lock lock(_drawExtentMutex);
		return _drawExtent;
	}
	void setDrawExtent(VkExtent3D extent) {
		std::scoped_lock lock(_drawExtentMutex);
		_drawExtent = extent;
	}

	uint32_t _frameNumber{ 0 };
	uint32_t _framesInFlight{ 0 };

	std::mutex _frameAccessMutex;

	FrameContext& getCurrentFrame() {
		std::scoped_lock lock(_frameAccessMutex);
		return *_frameContexts[_frameNumber % _framesInFlight];
	}

	TimelineSync _transferSync;
	TimelineSync _computeSync;

	void toneMapPass(FrameContext& frame, ColorData& toneMappingData);
	void geometryPass(std::array<VkImageView, 3> imageViews, FrameContext& frameCtx, Profiler& profiler);
}

void Renderer::initRenderer(
	const VkDevice device,
	const VkDescriptorSetLayout frameLayout,
	GPUResources& gpuResouces,
	bool isAssetsLoaded)
{
	SyncUtils::createTimelineSemaphore(_transferSync, device);

	if (GPU_ACCELERATION_ENABLED) {
		SyncUtils::createTimelineSemaphore(_computeSync, device);
	}

	_frameContexts = initFrameContexts(
		device,
		frameLayout,
		gpuResouces.getAllocator(),
		gpuResouces.stats,
		_framesInFlight,
		isAssetsLoaded
	);
}

void Renderer::prepareFrameContext(FrameContext& frameCtx) {
	auto device = Backend::getDevice();
	auto& swapDef = Backend::getSwapchainDef();

	VkFence fence = swapDef.inFlightFences[frameCtx.frameIndex];
	VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
	VK_CHECK(vkResetFences(device, 1, &fence));

	uint32_t imageIndex = 0;
	frameCtx.swapchainResult = vkAcquireNextImageKHR(
		device,
		swapDef.swapchain,
		UINT64_MAX,
		swapDef.imageAvailableSemaphores[frameCtx.frameIndex],
		VK_NULL_HANDLE,
		&imageIndex);

	if (frameCtx.swapchainResult == VK_ERROR_OUT_OF_DATE_KHR || frameCtx.swapchainResult == VK_SUBOPTIMAL_KHR) {
		Backend::getGraphicsQueue().waitIdle();
		Backend::resizeSwapchain();
		return;
	}
	ASSERT(frameCtx.swapchainResult == VK_SUCCESS && "Failed to acquire swapchain image!");

	frameCtx.swapchainImageIndex = imageIndex;

	// Mark image as in use
	swapDef.imageInFlightFrame[imageIndex] = frameCtx.frameIndex;

	VK_CHECK(vkResetCommandBuffer(frameCtx.commandBuffer, 0));

	frameCtx.freeStashedCmds(device);
	frameCtx.stagingHead = 0;

	frameCtx.cpuDeletion.flush();
}

void Renderer::submitFrame(FrameContext& frameCtx) {
	auto& swapDef = Backend::getSwapchainDef();
	uint32_t imageIndex = frameCtx.swapchainImageIndex;

	VkSemaphore presentSem = swapDef.imageAvailableSemaphores[frameCtx.frameIndex];

	// Use image-indexed render finished semaphore and fence
	VkSemaphore renderSem = swapDef.renderFinishedSemaphores[imageIndex];

	VkFence fence = swapDef.inFlightFences[frameCtx.frameIndex];

	std::vector<VkSemaphoreSubmitInfo> waitInfos;

	// Wait on image acquired semaphore
	VkSemaphoreSubmitInfo waitImageAvailable{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
	waitImageAvailable.semaphore = presentSem;
	waitImageAvailable.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	waitInfos.push_back(waitImageAvailable);

	// Wait on transfer timeline only up to the first buffer consumers
	if (_transferSync.signalValue) {
		VkSemaphoreSubmitInfo waitTransfer{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
		waitTransfer.semaphore = _transferSync.semaphore;
		waitTransfer.value = frameCtx.transferWaitValue;
		waitTransfer.stageMask =
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT |
			VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT |
			VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT; // earliest consumers of uploaded data
		waitInfos.push_back(waitTransfer);
	}

	if (GPU_ACCELERATION_ENABLED) {
		if (_computeSync.signalValue) {
			VkSemaphoreSubmitInfo waitCompute{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
			waitCompute.semaphore = _computeSync.semaphore;
			waitCompute.value = frameCtx.computeWaitValue;
			waitCompute.stageMask =
				VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT |
				VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
			waitInfos.push_back(waitCompute);
		}
	}

	VkCommandBufferSubmitInfo cmdInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
	cmdInfo.commandBuffer = frameCtx.commandBuffer;

	// Signal render finished semaphore
	VkSemaphoreSubmitInfo signalRender{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
	signalRender.semaphore = renderSem;
	signalRender.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;

	VkSubmitInfo2 submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
	submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitInfos.size());
	submitInfo.pWaitSemaphoreInfos = waitInfos.data();
	submitInfo.commandBufferInfoCount = 1;
	submitInfo.pCommandBufferInfos = &cmdInfo;
	submitInfo.signalSemaphoreInfoCount = 1;
	submitInfo.pSignalSemaphoreInfos = &signalRender;

	auto& gQueue = Backend::getGraphicsQueue();
	auto& pQueue = Backend::getPresentQueue();

	VK_CHECK(vkQueueSubmit2(gQueue.queue, 1, &submitInfo, fence));

	// Present using the image-indexed renderSem
	VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &renderSem;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &swapDef.swapchain;
	presentInfo.pImageIndices = &imageIndex;

	frameCtx.swapchainResult = vkQueuePresentKHR(pQueue.queue, &presentInfo);
	if (frameCtx.swapchainResult == VK_ERROR_OUT_OF_DATE_KHR || frameCtx.swapchainResult == VK_SUBOPTIMAL_KHR) {
		if (gQueue.queue != pQueue.queue)
			pQueue.waitIdle();
		else
			gQueue.waitIdle();

		Backend::resizeSwapchain();
	}
	else {
		ASSERT(frameCtx.swapchainResult == VK_SUCCESS && "Failed to present swapchain image!");
	}

	_frameNumber++;
}

void Renderer::recordRenderCommand(FrameContext& frameCtx, Profiler& profiler) {
	auto device = Backend::getDevice();
	auto& swp = Backend::getSwapchainDef();
	auto& draw = ResourceManager::getDrawImage();
	auto& msaa = ResourceManager::getMSAAImage();
	auto& depth = ResourceManager::getDepthImage();
	auto& toneMap = ResourceManager::getToneMappingImage();

	_drawExtent.width = std::min(swp.extent.width, draw.imageExtent.width);
	_drawExtent.height = std::min(swp.extent.height, draw.imageExtent.height);

	const auto unifiedSet = DescriptorSetOverwatch::getUnifiedDescriptors().descriptorSet;
	const VkDescriptorSet sets[2] {
		unifiedSet,
		frameCtx.set
	};

	VkCommandBufferBeginInfo cmdBeginInfo{};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(frameCtx.commandBuffer, &cmdBeginInfo));

	// Note: Currently only do cpu culling, once its in a compute this would need to be done way before main recording
	if (frameCtx.transformsBufferUploadNeeded) {
		const auto& globalAddrsTableBuf = Engine::getState().getGPUResources().getAddressTableBuffer();

		BarrierUtils::acquireShaderReadQ(frameCtx.commandBuffer, globalAddrsTableBuf);
		frameCtx.transformsBufferUploadNeeded = false;

		// Update the global set for transforms
		frameCtx.descriptorWriter.clear();
		frameCtx.descriptorWriter.writeBuffer(
			ADDRESS_TABLE_BINDING,
			globalAddrsTableBuf.buffer,
			sizeof(GPUAddressTable),
			0,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			unifiedSet);

		frameCtx.descriptorWriter.updateSet(device, unifiedSet);
	}

	if (frameCtx.visibleCount > 0) {
		BarrierUtils::acquireShaderReadQ(frameCtx.commandBuffer, frameCtx.addressTableBuffer);
	}

	frameCtx.writeFrameDescriptors(device);

	vkCmdBindDescriptorSets(frameCtx.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		Pipelines::_globalLayout.layout, 0, 2, sets, 0, nullptr);

	vkCmdBindDescriptorSets(frameCtx.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		Pipelines::_globalLayout.layout, 0, 2, sets, 0, nullptr);

	// color, depth and msaa transitions
	ImageUtils::transitionImage(
		frameCtx.commandBuffer, draw.image, draw.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	if (MSAA_ENABLED) {
		ImageUtils::transitionImage(
			frameCtx.commandBuffer, msaa.image, msaa.imageFormat,
			VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	}
	ImageUtils::transitionImage(
		frameCtx.commandBuffer, depth.image, depth.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	geometryPass({ draw.imageView, msaa.imageView, depth.imageView }, frameCtx, profiler);

	// ToneMapImage transition
	ImageUtils::transitionImage(
		frameCtx.commandBuffer, draw.image, draw.imageFormat,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

	ImageUtils::transitionImage(
		frameCtx.commandBuffer, toneMap.image, toneMap.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	toneMapPass(frameCtx, ResourceManager::toneMappingData);

	ImageUtils::transitionImage(
		frameCtx.commandBuffer, toneMap.image, toneMap.imageFormat,
		VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	ImageUtils::transitionImage(
		frameCtx.commandBuffer, swp.images[frameCtx.swapchainImageIndex], draw.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	ImageUtils::copyImageToImage(
		frameCtx.commandBuffer,
		toneMap.image,
		swp.images[frameCtx.swapchainImageIndex],
		{ _drawExtent.width, _drawExtent.height },
		swp.extent
	);

	const auto& debug = profiler.debugToggles;
	if (debug.enableSettings || debug.enableStats) {
		// Transition swapchain to COLOR_ATTACHMENT_OPTIMAL for ImGui
		ImageUtils::transitionImage(
			frameCtx.commandBuffer, swp.images[frameCtx.swapchainImageIndex], draw.imageFormat,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		EditorImgui::drawImgui(
			frameCtx.commandBuffer,
			swp.imageViews[frameCtx.swapchainImageIndex],
			swp.extent,
			false);

		ImageUtils::transitionImage(
			frameCtx.commandBuffer, swp.images[frameCtx.swapchainImageIndex], draw.imageFormat,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	}
	else {
		ImageUtils::transitionImage(
			frameCtx.commandBuffer, swp.images[frameCtx.swapchainImageIndex], draw.imageFormat,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	}

	VK_CHECK(vkEndCommandBuffer(frameCtx.commandBuffer));
}

// draw[0], msaa[1], depth[2]
void Renderer::geometryPass(std::array<VkImageView, 3> imageViews, FrameContext& frameCtx, Profiler& profiler) {
	VkRenderingAttachmentInfo colorAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
	colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.clearValue.color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

	// MSAA branch
	if (MSAA_ENABLED) {
		colorAttachment.imageView = imageViews[1];
		colorAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
		colorAttachment.resolveImageView = imageViews[0];
		colorAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	}
	else {
		colorAttachment.imageView = imageViews[0];
		colorAttachment.resolveMode = VK_RESOLVE_MODE_NONE;
		colorAttachment.resolveImageView = VK_NULL_HANDLE;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	}

	VkRenderingAttachmentInfo depthAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
	depthAttachment.imageView = imageViews[2];
	depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	depthAttachment.resolveMode = VK_RESOLVE_MODE_NONE;
	depthAttachment.resolveImageView = VK_NULL_HANDLE;
	depthAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthAttachment.clearValue.depthStencil.depth = 1.0f;

	VkRenderingInfo renderInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
	renderInfo.flags = 0,
	renderInfo.renderArea = { { 0, 0 }, { _drawExtent.width, _drawExtent.height } },
	renderInfo.layerCount = 1,
	renderInfo.viewMask = 0,
	renderInfo.colorAttachmentCount = 1,
	renderInfo.pColorAttachments = &colorAttachment,
	renderInfo.pDepthAttachment = &depthAttachment,

	vkCmdBeginRendering(frameCtx.commandBuffer, &renderInfo);
	VulkanUtils::defineViewportAndScissor(frameCtx.commandBuffer, { _drawExtent.width, _drawExtent.height });
	RenderScene::renderGeometry(frameCtx, profiler);
	vkCmdEndRendering(frameCtx.commandBuffer);
}

// TODO: Make this better, like gltf viewer tonemapper slider
void Renderer::toneMapPass(FrameContext& frame, ColorData& toneMappingData) {
	vkCmdBindPipeline(
		frame.commandBuffer,
		VK_PIPELINE_BIND_POINT_COMPUTE,
		Pipelines::getPipelineByID(PipelineID::ToneMap));

	vkCmdPushConstants(frame.commandBuffer,
		Pipelines::_globalLayout.layout,
		Pipelines::_globalLayout.pcRange.stageFlags,
		Pipelines::_globalLayout.pcRange.offset,
		Pipelines::_globalLayout.pcRange.size,
		&toneMappingData);

	const auto xDispatch = static_cast<uint32_t>(std::ceil(_drawExtent.width / 16.0f));
	const auto yDispatch = static_cast<uint32_t>(std::ceil(_drawExtent.height / 16.0f));

	vkCmdDispatch(frame.commandBuffer, xDispatch, yDispatch, 1);
}


void Renderer::cleanupRenderer(const VkDevice device, const VmaAllocator alloc) {
	cleanupFrameContexts(_frameContexts, device, alloc);

	if (_transferSync.semaphore != VK_NULL_HANDLE)
		vkDestroySemaphore(device, _transferSync.semaphore, nullptr);

	if (_computeSync.semaphore != VK_NULL_HANDLE)
		vkDestroySemaphore(device, _computeSync.semaphore, nullptr);
}