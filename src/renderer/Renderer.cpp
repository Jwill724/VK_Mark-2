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
	void geometryPass(std::array<VkImageView, 3> imageViews, FrameContext& frameCtx, Profiler& profiler);
}

void Renderer::initFrameContexts(
	VkDevice device,
	VkDescriptorSetLayout frameLayout,
	const VmaAllocator allocator,
	const uint32_t totalVertexCount,
	const uint32_t totalIndexCount,
	bool isAssetsLoaded)
{
	auto& swapDef = Backend::getSwapchainDef();
	framesInFlight = swapDef.imageCount;

	_frameContexts.resize(framesInFlight);

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
			TRANSPARENT_INDIRECT_SIZE_BYTES +
			TRANSFORM_LIST_SIZE_BYTES;
	}

	fmt::print("Frames in flight:[{}]\n", framesInFlight);

	for (uint32_t i = 0; i < framesInFlight; ++i) {
		auto frame = std::make_unique<FrameContext>();
		frame->frameIndex = i;

		frame->graphicsPool = CommandBuffer::createCommandPool(device, graphicsIndex);
		frame->transferPool = CommandBuffer::createCommandPool(device, transferIndex);
		frame->commandBuffer = CommandBuffer::createCommandBuffer(device, frame->graphicsPool);
		frame->set = DescriptorSetOverwatch::mainDescriptorManager.allocateDescriptor(device, frameLayout);
		frame->transferDeletion.semaphore = _transferSync.semaphore;

		if (GPU_ACCELERATION_ENABLED) {
			frame->computePool = CommandBuffer::createCommandPool(device, computeIndex);
			frame->computeDeletion.semaphore = _computeSync.semaphore;
		}

		frame->addressTableBuffer = BufferUtils::createBuffer(
			sizeof(GPUAddressTable),
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT |
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VMA_MEMORY_USAGE_GPU_ONLY,
			allocator);

		frame->drawData.totalVertexCount = totalVertexCount;
		frame->drawData.totalIndexCount = totalIndexCount;

		if (totalGPUStagingSize > 0) {
			frame->addressTableStaging = BufferUtils::createBuffer(
				sizeof(GPUAddressTable),
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
				allocator);
			ASSERT(frame->addressTableStaging.info.pMappedData);

			frame->combinedGPUStaging = BufferUtils::createBuffer(
				totalGPUStagingSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
				allocator);
			ASSERT(frame->combinedGPUStaging.info.pMappedData);

			frame->opaqueInstanceBuffer = BufferUtils::createGPUAddressBuffer(
				AddressBufferType::OpaqueIntances, frame->addressTable, OPAQUE_INSTANCE_SIZE_BYTES, allocator);
			frame->persistentGPUBuffers.push_back(frame->opaqueInstanceBuffer);

			frame->opaqueIndirectCmdBuffer = BufferUtils::createGPUAddressBuffer(
				AddressBufferType::OpaqueIndirectDraws, frame->addressTable, OPAQUE_INDIRECT_SIZE_BYTES, allocator);
			frame->persistentGPUBuffers.push_back(frame->opaqueIndirectCmdBuffer);

			frame->transparentInstanceBuffer = BufferUtils::createGPUAddressBuffer(
				AddressBufferType::TransparentInstances, frame->addressTable, TRANSPARENT_INSTANCE_SIZE_BYTES, allocator);
			frame->persistentGPUBuffers.push_back(frame->transparentInstanceBuffer);

			frame->transparentIndirectCmdBuffer = BufferUtils::createGPUAddressBuffer(
				AddressBufferType::TransparentIndirectDraws, frame->addressTable, TRANSPARENT_INDIRECT_SIZE_BYTES, allocator);
			frame->persistentGPUBuffers.push_back(frame->transparentIndirectCmdBuffer);

			frame->transformsListBuffer = BufferUtils::createGPUAddressBuffer(
				AddressBufferType::Transforms, frame->addressTable, TRANSFORM_LIST_SIZE_BYTES, allocator);
			frame->persistentGPUBuffers.push_back(frame->transformsListBuffer);
		}

		_frameContexts[i] = std::move(frame);
	}
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
		swapDef.presentSemaphores[frameCtx.frameIndex],
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

	if (!frameCtx.transferCmds.empty()) {
		vkFreeCommandBuffers(device, frameCtx.transferPool,
			static_cast<uint32_t>(frameCtx.transferCmds.size()),
			frameCtx.transferCmds.data());
		frameCtx.transferCmds.clear();
		frameCtx.transferDeletion.process(device);
	}

	if (!frameCtx.computeCmds.empty()) {
		vkFreeCommandBuffers(device, frameCtx.computePool,
			static_cast<uint32_t>(frameCtx.computeCmds.size()),
			frameCtx.computeCmds.data());
		frameCtx.computeCmds.clear();
		frameCtx.computeDeletion.process(device);
	}

	frameCtx.cpuDeletion.flush();
}

void Renderer::submitFrame(FrameContext& frameCtx) {
	auto& swapDef = Backend::getSwapchainDef();
	uint32_t imageIndex = frameCtx.swapchainImageIndex;

	VkSemaphore presentSem = swapDef.presentSemaphores[frameCtx.frameIndex];

	// Use image-indexed render finished semaphore and fence
	VkSemaphore renderSem = swapDef.renderFinishedSemaphores[imageIndex];

	VkFence fence = swapDef.inFlightFences[frameCtx.frameIndex];

	std::vector<VkSemaphoreSubmitInfo> waitInfos;

	// Wait on image acquired semaphore
	VkSemaphoreSubmitInfo waitImageAvailable{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
	waitImageAvailable.semaphore = presentSem;
	waitImageAvailable.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	waitInfos.push_back(waitImageAvailable);

	if (frameCtx.transferWaitValue <= _transferSync.signalValue - 1) {
		VkSemaphoreSubmitInfo waitTransfer{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
		waitTransfer.semaphore = _transferSync.semaphore;
		waitTransfer.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		waitTransfer.value = frameCtx.transferWaitValue;
		waitInfos.push_back(waitTransfer);
	}

	if (GPU_ACCELERATION_ENABLED) {
		if (frameCtx.computeWaitValue <= _computeSync.signalValue - 1) {
			VkSemaphoreSubmitInfo waitCompute{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
			waitCompute.semaphore = _computeSync.semaphore;
			waitCompute.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			waitCompute.value = frameCtx.computeWaitValue;
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
	Backend::getTransferQueue().waitTimelineValue(
		device,
		_transferSync.semaphore,
		frameCtx.transferWaitValue
	);

	auto& swp = Backend::getSwapchainDef();
	auto& draw = ResourceManager::getDrawImage();
	auto& msaa = ResourceManager::getMSAAImage();
	auto& depth = ResourceManager::getDepthImage();
	auto& toneMap = ResourceManager::getToneMappingImage();

	_drawExtent.width = static_cast<uint32_t>(std::min(swp.extent.width, draw.imageExtent.width));
	_drawExtent.height = static_cast<uint32_t>(std::min(swp.extent.height, draw.imageExtent.height));

	const VkDescriptorSet sets[2] {
		DescriptorSetOverwatch::getUnifiedDescriptors().descriptorSet,
		frameCtx.set
	};

	VkCommandBufferBeginInfo cmdBeginInfo{};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(frameCtx.commandBuffer, &cmdBeginInfo));

	if (!frameCtx.transferCmds.empty()) {
		if (frameCtx.opaqueVisibleCount > 0) {
			RendererUtils::insertIndirectDrawBufferBarrier(frameCtx.commandBuffer, frameCtx.opaqueIndirectCmdBuffer.buffer);
			RendererUtils::insertTransferToGraphicsBufferBarrier(frameCtx.commandBuffer, frameCtx.opaqueInstanceBuffer.buffer);
		}
		if (frameCtx.transparentVisibleCount > 0) {
			RendererUtils::insertIndirectDrawBufferBarrier(frameCtx.commandBuffer, frameCtx.transparentIndirectCmdBuffer.buffer);
			RendererUtils::insertTransferToGraphicsBufferBarrier(frameCtx.commandBuffer, frameCtx.transparentInstanceBuffer.buffer);
		}

		RendererUtils::insertTransferToGraphicsBufferBarrier(frameCtx.commandBuffer, frameCtx.transformsListBuffer.buffer);
		RendererUtils::insertTransferToGraphicsBufferBarrier(frameCtx.commandBuffer, frameCtx.addressTableBuffer.buffer);
	}

	// Depending on if theres visibles, this could be the first and only write for the storage buffer
	// If no visibles are present, early outs upload and table isn't marked dirty
	bool descriptorWriteNeeded = false;
	if (frameCtx.addressTableDirty) {
		frameCtx.writer.clear();
		frameCtx.writer.writeBuffer(
			0,
			frameCtx.addressTableBuffer.buffer,
			frameCtx.addressTableBuffer.info.size,
			0,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			frameCtx.set
		);
		descriptorWriteNeeded = true;
		frameCtx.addressTableDirty = false;
	}

	if (!descriptorWriteNeeded) {
		frameCtx.writer.clear(); // Only clear if it wasn't already cleared
	}

	frameCtx.writer.writeBuffer(
		1,
		frameCtx.sceneDataBuffer.buffer,
		sizeof(GPUSceneData),
		0,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		frameCtx.set
	);
	frameCtx.writer.updateSet(device, frameCtx.set);

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

	geometryPass({ draw.imageView, msaa.imageView, depth.imageView }, frameCtx, profiler);

	// ToneMapImage transition and copy
	RendererUtils::transitionImage(
		frameCtx.commandBuffer,
		draw.image,
		draw.imageFormat,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	RendererUtils::transitionImage(
		frameCtx.commandBuffer,
		toneMap.image,
		toneMap.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	RendererUtils::copyImageToImage(
		frameCtx.commandBuffer,
		draw.image,
		toneMap.image,
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
		toneMap.image,
		toneMap.imageFormat,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_GENERAL);

	colorCorrectPass(frameCtx, ResourceManager::toneMappingData);

	RendererUtils::transitionImage(
		frameCtx.commandBuffer,
		toneMap.image,
		toneMap.imageFormat,
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
		toneMap.image,
		swp.images[frameCtx.swapchainImageIndex],
		{ _drawExtent.width, _drawExtent.height },
		swp.extent
	);

	const auto& debug = profiler.debugToggles;
	if (debug.enableSettings || debug.enableStats) {
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
	}
	else {
		RendererUtils::transitionImage(
			frameCtx.commandBuffer,
			swp.images[frameCtx.swapchainImageIndex],
			draw.imageFormat,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	}

	VK_CHECK(vkEndCommandBuffer(frameCtx.commandBuffer));
}

// draw[0], msaa[1], depth[2]
void Renderer::geometryPass(std::array<VkImageView, 3> imageViews, FrameContext& frameCtx, Profiler& profiler) {
	VkRenderingAttachmentInfo colorAttachment {
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

	VkRenderingAttachmentInfo depthAttachment {
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

	VkRenderingInfo renderInfo {
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

	vkCmdBeginRendering(frameCtx.commandBuffer, &renderInfo);
	VulkanUtils::defineViewportAndScissor(frameCtx.commandBuffer, { _drawExtent.width, _drawExtent.height });
	RenderScene::renderGeometry(frameCtx, profiler);
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

	for (auto& framePtr : _frameContexts) {
		if (!framePtr) continue;

		auto& frame = *framePtr;

		frame.cpuDeletion.flush();

		for (auto& buf : frame.persistentGPUBuffers)
			BufferUtils::destroyAllocatedBuffer(buf, allocator);

		frame.transferCmds.clear();
		frame.transferDeletion.process(device);

		frame.computeCmds.clear();
		frame.computeDeletion.process(device);

		if (frame.graphicsPool != VK_NULL_HANDLE)
			vkDestroyCommandPool(device, frame.graphicsPool, nullptr);

		if (frame.transferPool != VK_NULL_HANDLE)
			vkDestroyCommandPool(device, frame.transferPool, nullptr);

		if (frame.computePool != VK_NULL_HANDLE)
			vkDestroyCommandPool(device, frame.computePool, nullptr);

		if (frame.stagingVisibleMeshIDsBuffer.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(frame.stagingVisibleMeshIDsBuffer, allocator);

		if (frame.stagingVisibleCountBuffer.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(frame.stagingVisibleCountBuffer, allocator);

		if (frame.gpuVisibleCountBuffer.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(frame.gpuVisibleCountBuffer, allocator);

		if (frame.gpuVisibleMeshIDsBuffer.buffer != VK_NULL_HANDLE)
			BufferUtils::destroyAllocatedBuffer(frame.gpuVisibleMeshIDsBuffer, allocator);

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

	_frameContexts.clear();
}