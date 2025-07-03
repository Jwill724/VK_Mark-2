#include "pch.h"

#include "Renderer.h"
#include "profiler/EditorImgui.h"
#include "vulkan/Backend.h"
#include "RenderScene.h"
#include "SceneGraph.h"
#include "utils/RendererUtils.h"
#include "core/AssetManager.h"
#include "gpu_types/PipelineManager.h"
#include "gpu_types/CommandBuffer.h"

#include "common/ResourceTypes.h"

namespace Renderer {
	TimelineSync _transferSync;
	TimelineSync _computeSync;

	void colorCorrectPass(FrameContext& frame, ColorData& toneMappingData);
	void geometryPass(std::array<VkImageView, 3> imageViews, FrameContext& frameCtx);
}

void Renderer::initFrameContexts(
	VkDevice device,
	VkDescriptorSetLayout layout,
	const VmaAllocator allocator,
	bool isAssetsLoaded)
{
	uint32_t graphicsIndex = Backend::getGraphicsQueue().familyIndex;
	uint32_t transferIndex = Backend::getTransferQueue().familyIndex;
	uint32_t computeIndex = Backend::getComputeQueue().familyIndex;

	RendererUtils::createTimelineSemaphore(_transferSync);

	if (GPU_ACCELERATION_ENABLED) {
		RendererUtils::createTimelineSemaphore(_computeSync);
	}

	size_t totalGPUStagingSize = 0;
	if (isAssetsLoaded) {
		totalGPUStagingSize =
			OPAQUE_INSTANCE_SIZE_BYTES +
			OPAQUE_INDIRECT_SIZE_BYTES +
			TRANSPARENT_INSTANCE_SIZE_BYTES +
			TRANSPARENT_INDIRECT_SIZE_BYTES;
	}

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
		auto& frame = _frameContexts[i];
		frame.syncObjs.swapchainSemaphore = RendererUtils::createSemaphore();
		frame.syncObjs.semaphore = RendererUtils::createSemaphore();
		frame.syncObjs.fence = RendererUtils::createFence();
		frame.graphicsPool = CommandBuffer::createCommandPool(device, graphicsIndex);
		frame.transferPool = CommandBuffer::createCommandPool(device, transferIndex);
		frame.commandBuffer = CommandBuffer::createCommandBuffer(device, frame.graphicsPool);
		frame.set = DescriptorSetOverwatch::mainDescriptorManager.allocateDescriptor(device, layout);
		frame.transferDeletion.semaphore = _transferSync.semaphore;

		if (GPU_ACCELERATION_ENABLED) {
			frame.computePool = CommandBuffer::createCommandPool(device, computeIndex);
			frame.computeDeletion.semaphore = _computeSync.semaphore;
		}

		frame.addressTableBuffer = BufferUtils::createBuffer(
			sizeof(GPUAddressTable),
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT |
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VMA_MEMORY_USAGE_GPU_ONLY,
			allocator);

		if (totalGPUStagingSize > 0) {
			frame.addressTableStaging = BufferUtils::createBuffer(
				sizeof(GPUAddressTable),
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
				allocator);
			ASSERT(frame.addressTableStaging.info.pMappedData);

			frame.combinedGPUStaging = BufferUtils::createBuffer(
				totalGPUStagingSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
				allocator);
			ASSERT(frame.combinedGPUStaging.info.pMappedData);
		}

		frame.frameIndex = i;
	}
}

void Renderer::prepareFrameContext(FrameContext& frameCtx) {
	auto device = Backend::getDevice();

	VK_CHECK(vkWaitForFences(device, 1, &frameCtx.syncObjs.fence, VK_TRUE, UINT64_MAX));
	VK_CHECK(vkResetFences(device, 1, &frameCtx.syncObjs.fence));

	if (!frameCtx.transferCmds.empty()) {
		vkFreeCommandBuffers(
			device,
			frameCtx.transferPool,
			static_cast<uint32_t>(frameCtx.transferCmds.size()),
			frameCtx.transferCmds.data());
		frameCtx.transferCmds.clear();
		frameCtx.transferDeletion.process(device);
	}
	if (!frameCtx.computeCmds.empty()) {
		vkFreeCommandBuffers(
			device,
			frameCtx.computePool,
			static_cast<uint32_t>(frameCtx.computeCmds.size()),
			frameCtx.computeCmds.data());
		frameCtx.computeCmds.clear();
		frameCtx.computeDeletion.process(device);
	}

	frameCtx.cpuDeletion.flush();

	frameCtx.swapchainResult = vkAcquireNextImageKHR(
		device,
		Backend::getSwapchainDef().swapchain,
		UINT64_MAX,
		frameCtx.syncObjs.swapchainSemaphore,
		VK_NULL_HANDLE,
		&frameCtx.swapchainImageIndex);
	if (frameCtx.swapchainResult == VK_ERROR_OUT_OF_DATE_KHR || frameCtx.swapchainResult == VK_SUBOPTIMAL_KHR) {
		Backend::getGraphicsQueue().waitIdle();
		Backend::resizeSwapchain();
		return;
	}
	else {
		ASSERT(frameCtx.swapchainResult == VK_SUCCESS && "Failed to present swap chain image!");
	}

	VK_CHECK(vkResetCommandBuffer(frameCtx.commandBuffer, 0));
}

void Renderer::submitFrame(FrameContext& frameCtx) {
	//fmt::print("=== SUBMIT FRAME [{}] ===\n", _frameNumber);
	//fmt::print("FrameIndex: {}\n", frameCtx.frameIndex);
	//fmt::print("CommandBuffer: {}\n", static_cast<void*>(frameCtx.commandBuffer));
	//fmt::print("Fence: {}\n", static_cast<void*>(frameCtx.syncObjs.fence));
	//fmt::print("WaitSemaphore: {} | SignalSemaphore: {}\n",
	//	static_cast<void*>(frameCtx.syncObjs.swapchainSemaphore),
	//	static_cast<void*>(frameCtx.syncObjs.semaphore));
	//fmt::print("Swapchain Image Index: {}\n", frameCtx.swapchainImageIndex);


	VkCommandBufferSubmitInfo cmdInfo{};
	cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	cmdInfo.pNext = nullptr;
	cmdInfo.commandBuffer = frameCtx.commandBuffer;
	cmdInfo.deviceMask = 0;

	// WAIT: Swapchain acquire
	VkSemaphoreSubmitInfo waitSwapchain{};
	waitSwapchain.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	waitSwapchain.semaphore = frameCtx.syncObjs.swapchainSemaphore;
	waitSwapchain.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR;
	waitSwapchain.deviceIndex = 0;
	waitSwapchain.value = 1;

	// WAIT: Transfer completion
	VkSemaphoreSubmitInfo waitTransfer{};
	if (frameCtx.transferWaitValue <= _transferSync.signalValue - 1) {
		ASSERT(_transferSync.semaphore != VK_NULL_HANDLE && "[Transfer] Timeline semaphore must be set before rendering");
		waitTransfer.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
		waitTransfer.semaphore = _transferSync.semaphore;
		waitTransfer.stageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
		waitTransfer.deviceIndex = 0;
		waitTransfer.value = frameCtx.transferWaitValue;
	}

	// WAIT: Compute completion
	VkSemaphoreSubmitInfo waitCompute{};
	if (GPU_ACCELERATION_ENABLED) {
		if (frameCtx.computeWaitValue <= _computeSync.signalValue - 1) {
			ASSERT(_computeSync.semaphore != VK_NULL_HANDLE && "[Compute] Timeline semaphore must be set before rendering");
			waitCompute.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
			waitCompute.semaphore = _computeSync.semaphore;
			waitCompute.stageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
			waitCompute.deviceIndex = 0;
			waitCompute.value = frameCtx.computeWaitValue;
		}
	}


	// SIGNAL: Rendering done
	VkSemaphoreSubmitInfo signalRender{};
	signalRender.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	signalRender.semaphore = frameCtx.syncObjs.semaphore;
	signalRender.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
	signalRender.deviceIndex = 0;
	signalRender.value = 1;

	std::vector<VkSemaphoreSubmitInfo> waitInfos;
	if (waitSwapchain.semaphore != VK_NULL_HANDLE) waitInfos.push_back(waitSwapchain);
	if (waitTransfer.semaphore != VK_NULL_HANDLE) waitInfos.push_back(waitTransfer);
	if (waitCompute.semaphore != VK_NULL_HANDLE) waitInfos.push_back(waitCompute);


	VkSubmitInfo2 graphicsSubmitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
	graphicsSubmitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitInfos.size());
	graphicsSubmitInfo.pWaitSemaphoreInfos = waitInfos.empty() ? nullptr : waitInfos.data();
	graphicsSubmitInfo.signalSemaphoreInfoCount = signalRender.semaphore != VK_NULL_HANDLE ? 1 : 0;
	graphicsSubmitInfo.pSignalSemaphoreInfos = signalRender.semaphore != VK_NULL_HANDLE ? &signalRender : nullptr;
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

	if (frameCtx.swapchainResult == VK_ERROR_OUT_OF_DATE_KHR || frameCtx.swapchainResult == VK_SUBOPTIMAL_KHR) {
		if (graphicsQueue.queue != presentQueue.queue) {
			presentQueue.waitIdle();
		}
		else {
			graphicsQueue.waitIdle();
		}

		Backend::resizeSwapchain();
	}
	else {
		ASSERT(frameCtx.swapchainResult == VK_SUCCESS && "Failed to present swap chain image!");
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

	_drawExtent.width = static_cast<uint32_t>(std::min(swp.extent.width, draw.imageExtent.width));
	_drawExtent.height = static_cast<uint32_t>(std::min(swp.extent.height, draw.imageExtent.height));

	const VkDescriptorSet sets[2] = {
		DescriptorSetOverwatch::getUnifiedDescriptors().descriptorSet,
		frameCtx.set
	};

	VK_CHECK(vkBeginCommandBuffer(frameCtx.commandBuffer, &cmdBeginInfo));

	vkCmdBindDescriptorSets(frameCtx.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		Pipelines::_globalLayout.layout, 0, 2, sets, 0, nullptr);

	vkCmdBindDescriptorSets(frameCtx.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		Pipelines::_globalLayout.layout, 0, 2, sets, 0, nullptr);

	// color, depth and msaa transitions
	RendererUtils::transitionImage(
		frameCtx.commandBuffer,
		draw.image,
		draw.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	if (MSAA_ENABLED) {
		RendererUtils::transitionImage(
			frameCtx.commandBuffer,
			msaa.image,
			msaa.imageFormat,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	}
	RendererUtils::transitionImage(
		frameCtx.commandBuffer,
		depth.image,
		depth.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	geometryPass({ draw.imageView, msaa.imageView, depth.imageView }, frameCtx);

	// Post process transition and copy
	RendererUtils::transitionImage(frameCtx.commandBuffer,
		draw.image,
		draw.imageFormat,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	RendererUtils::transitionImage(
		frameCtx.commandBuffer,
		postProcess.image,
		postProcess.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	RendererUtils::copyImageToImage(
		frameCtx.commandBuffer,
		draw.image,
		postProcess.image,
		{ _drawExtent.width, _drawExtent.height },
		{ _drawExtent.width, _drawExtent.height }
	);

	RendererUtils::transitionImage(
		frameCtx.commandBuffer,
		draw.image,
		draw.imageFormat,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		VK_IMAGE_LAYOUT_GENERAL);
	RendererUtils::transitionImage(
		frameCtx.commandBuffer,
		postProcess.image,
		postProcess.imageFormat,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_GENERAL);

	colorCorrectPass(frameCtx, ResourceManager::toneMappingData);

	RendererUtils::transitionImage(
		frameCtx.commandBuffer,
		postProcess.image,
		postProcess.imageFormat,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	RendererUtils::transitionImage(
		frameCtx.commandBuffer,
		swp.images[frameCtx.swapchainImageIndex],
		draw.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	RendererUtils::copyImageToImage(
		frameCtx.commandBuffer,
		postProcess.image,
		swp.images[frameCtx.swapchainImageIndex],
		{ _drawExtent.width, _drawExtent.height },
		swp.extent
	);

	// Transition swapchain to COLOR_ATTACHMENT_OPTIMAL for ImGui
	RendererUtils::transitionImage(
		frameCtx.commandBuffer,
		swp.images[frameCtx.swapchainImageIndex],
		draw.imageFormat,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	EditorImgui::drawImgui(frameCtx.commandBuffer, swp.imageViews[frameCtx.swapchainImageIndex], false);

	RendererUtils::transitionImage(
		frameCtx.commandBuffer,
		swp.images[frameCtx.swapchainImageIndex],
		draw.imageFormat,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	VK_CHECK(vkEndCommandBuffer(frameCtx.commandBuffer));
}

// draw[0], msaa[1], depth[2]
void Renderer::geometryPass(std::array<VkImageView, 3> imageViews, FrameContext& frameCtx) {
	VkRenderingAttachmentInfo colorAttachment = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.pNext = nullptr,
		.imageView = !MSAA_ENABLED ? imageViews[0] : imageViews[1],
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.resolveMode = !MSAA_ENABLED ? VK_RESOLVE_MODE_NONE : VK_RESOLVE_MODE_AVERAGE_BIT,
		.resolveImageView = !MSAA_ENABLED ? VK_NULL_HANDLE : imageViews[0],
		.resolveImageLayout = !MSAA_ENABLED ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
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
	depthAttachment.clearValue.depthStencil.depth = 1.0f;

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

	if (!frameCtx.transferCmds.empty()) {
		if (frameCtx.opaqueVisibleCount > 0 && frameCtx.opaqueInstanceBuffer.buffer != VK_NULL_HANDLE) {
			RendererUtils::insertTransferToGraphicsBufferBarrier(frameCtx.commandBuffer, frameCtx.opaqueInstanceBuffer.buffer);
			RendererUtils::insertTransferToGraphicsBufferBarrier(frameCtx.commandBuffer, frameCtx.opaqueIndirectCmdBuffer.buffer);
		}
		if (frameCtx.transparentVisibleCount > 0 && frameCtx.transparentInstanceBuffer.buffer != VK_NULL_HANDLE) {
			RendererUtils::insertTransferToGraphicsBufferBarrier(frameCtx.commandBuffer, frameCtx.transparentInstanceBuffer.buffer);
			RendererUtils::insertTransferToGraphicsBufferBarrier(frameCtx.commandBuffer, frameCtx.transparentIndirectCmdBuffer.buffer);
		}

		RendererUtils::insertTransferToGraphicsBufferBarrier(frameCtx.commandBuffer, frameCtx.addressTableBuffer.buffer);
	}

	vkCmdBeginRendering(frameCtx.commandBuffer, &renderInfo);
	RenderScene::renderGeometry(frameCtx);
	vkCmdEndRendering(frameCtx.commandBuffer);
}

// TODO: Make this better, like gltf viewer tonemapper slider
void Renderer::colorCorrectPass(FrameContext& frame, ColorData& toneMappingData) {
	vkCmdBindPipeline(frame.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, Pipelines::postProcessPipeline.pipeline);

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

// cleans up frame contexts
void Renderer::cleanup() {
	auto device = Backend::getDevice();
	auto allocator = Engine::getState().getGPUResources().getAllocator();

	for (auto& frame : _frameContexts) {
		frame.cpuDeletion.flush();

		if (frame.transferDeletion.semaphore != VK_NULL_HANDLE)
			frame.transferDeletion.process(device);

		if (!frame.transferCmds.empty())
			frame.transferCmds.clear();

		if (frame.computeDeletion.semaphore != VK_NULL_HANDLE)
			frame.computeDeletion.process(device);

		if (!frame.computeCmds.empty())
			frame.computeCmds.clear();

		if (frame.syncObjs.fence != VK_NULL_HANDLE)
			vkDestroyFence(device, frame.syncObjs.fence, nullptr);
		if (frame.syncObjs.semaphore != VK_NULL_HANDLE)
			vkDestroySemaphore(device, frame.syncObjs.semaphore, nullptr);
		if (frame.syncObjs.swapchainSemaphore != VK_NULL_HANDLE)
			vkDestroySemaphore(device, frame.syncObjs.swapchainSemaphore, nullptr);

		if (frame.graphicsPool != VK_NULL_HANDLE)
			vkDestroyCommandPool(device, frame.graphicsPool, nullptr);

		if (frame.transferPool != VK_NULL_HANDLE)
			vkDestroyCommandPool(device, frame.transferPool, nullptr);

		if (frame.computePool != VK_NULL_HANDLE)
			vkDestroyCommandPool(device, frame.computePool, nullptr);

		if (frame.transformsListBuffer.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(frame.transformsListBuffer, allocator);

		if (frame.stagingVisibleMeshIDsBuffer.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(frame.stagingVisibleMeshIDsBuffer, allocator);

		if (frame.stagingVisibleCountBuffer.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(frame.stagingVisibleCountBuffer, allocator);

		if (frame.gpuVisibleCountBuffer.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(frame.gpuVisibleCountBuffer, allocator);

		if (frame.gpuVisibleMeshIDsBuffer.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(frame.gpuVisibleMeshIDsBuffer, allocator);

		// address buffers last
		if (frame.combinedGPUStaging.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(frame.combinedGPUStaging, allocator);

		if (frame.addressTableStaging.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(frame.addressTableStaging, allocator);

		if (frame.addressTableBuffer.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(frame.addressTableBuffer, allocator);
	}

	if (_transferSync.semaphore != VK_NULL_HANDLE)
		vkDestroySemaphore(device, _transferSync.semaphore, nullptr);

	if (_computeSync.semaphore != VK_NULL_HANDLE)
		vkDestroySemaphore(device, _computeSync.semaphore, nullptr);
}