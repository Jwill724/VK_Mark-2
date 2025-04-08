#include "pch.h"

#include "Renderer.h"
#include "renderer/CommandBuffer.h"
#include "vulkan/PipelineManager.h"
#include "imgui/EditorImgui.h"
#include "vulkan/Backend.h"
#include "RenderScene.h"
#include "SceneGraph.h"

namespace Renderer {
	unsigned int _frameNumber{ 0 };

	FrameData _frames[MAX_FRAMES_IN_FLIGHT];
	FrameData& getCurrentFrame() { return _frames[_frameNumber % MAX_FRAMES_IN_FLIGHT]; }

	VkExtent3D _drawExtent;
	VkExtent3D getDrawExtent() { return _drawExtent; }

	float _renderScale = 1.f;
	float& getRenderScale() { return _renderScale; }

	// primary render image
	AllocatedImage _drawImage;
	AllocatedImage& getDrawImage() { return _drawImage; }
	AllocatedImage _depthImage;
	AllocatedImage& getDepthImage() { return _depthImage; }
	AllocatedImage _msaaImage;
	AllocatedImage& getMSAAImage() { return _msaaImage; }

	AllocatedImage _postProcessImage;
	AllocatedImage& getPostProcessImage() { return _postProcessImage; }

	VkDescriptorSetLayout _drawImageDescriptorLayout;

	VkImageView& getDrawImageView() { return _drawImage.imageView; }
	DeletionQueue _renderImageDeletionQueue;
	DeletionQueue& getRenderImageDeletionQueue() { return _renderImageDeletionQueue; }

	VmaAllocator _renderImageAllocator;
	VmaAllocator& getRenderImageAllocator() { return _renderImageAllocator; }

	void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex);
	void drawBackground(VkCommandBuffer cmd);
	void drawGeometry(VkCommandBuffer cmd);

	std::vector<VkSampleCountFlags> _availableSampleCounts;
	std::vector<VkSampleCountFlags>& getAvailableSampleCounts() { return _availableSampleCounts; }
	uint32_t _currentSampleCount = 1u;
	uint32_t getCurrentSampleCount() { return _currentSampleCount; }
}

void Renderer::setupRenderImages() {
	// draw image should match window extent
	_drawExtent = {
		Engine::getWindowExtent().width,
		Engine::getWindowExtent().height,
		1
	};

	// hardcoding the draw format to 32 bit float
	_drawImage.imageFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
	_drawImage.imageExtent = _drawExtent;
	_drawImage.mipmapped = false;

	VkImageUsageFlags drawImageUsages{};
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_STORAGE_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	drawImageUsages |= VK_IMAGE_USAGE_SAMPLED_BIT;

	// non sampled image
	// primary draw image color target
	RendererUtils::createRenderImage(_drawImage, drawImageUsages,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SAMPLE_COUNT_1_BIT, Renderer::getRenderImageDeletionQueue(), Renderer::getRenderImageAllocator());

	// post process image
	_postProcessImage.imageFormat = _drawImage.imageFormat;
	_postProcessImage.imageExtent = _drawExtent;
	_postProcessImage.mipmapped = false;

	VkImageUsageFlags postUsages{};
	postUsages |= VK_IMAGE_USAGE_STORAGE_BIT;            // for compute shader write
	postUsages |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;       // to copy to swapchain
	postUsages |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;       // if needed in chain

	// compute draw image for post processing
	RendererUtils::createRenderImage(_postProcessImage, postUsages,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_SAMPLE_COUNT_1_BIT, Renderer::getRenderImageDeletionQueue(), Renderer::getRenderImageAllocator());


	// MSAA SETTING  8 is the max allowed
	// TODO: set this up somewhere besides here
	_currentSampleCount = 4u;
	VkSampleCountFlagBits sampleCount = static_cast<VkSampleCountFlagBits>(_currentSampleCount);

	_msaaImage.imageFormat = _drawImage.imageFormat;
	_msaaImage.imageExtent = _drawExtent;
	_msaaImage.mipmapped = false;

	VkImageUsageFlags msaaImageUsages{};
	msaaImageUsages |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	msaaImageUsages |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;

	// msaa color attachment to the draw image
	RendererUtils::createRenderImage(_msaaImage, msaaImageUsages,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sampleCount, Renderer::getRenderImageDeletionQueue(), Renderer::getRenderImageAllocator());


	// DEPTH
	_depthImage.imageFormat = VK_FORMAT_D32_SFLOAT;
	_depthImage.imageExtent = _drawExtent;
	_depthImage.mipmapped = false;

	VkImageUsageFlags depthImageUsages{};
	depthImageUsages |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	RendererUtils::createRenderImage(_depthImage, depthImageUsages,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, sampleCount, Renderer::getRenderImageDeletionQueue(), Renderer::getRenderImageAllocator());
}

void Renderer::init() {
	QueueFamilyIndices qFamIndices = Backend::getQueueFamilyIndices();

	for (auto& frame : _frames) {
		frame._graphicsCmdPool = CommandBuffer::createCommandPool(qFamIndices.graphicsFamily.value(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
		frame._graphicsCmdBuffer = CommandBuffer::createCommandBuffer(frame._graphicsCmdPool);

		frame._swapchainSemaphore = RendererUtils::createSemaphore();
		frame._renderSemaphore = RendererUtils::createSemaphore();
		frame._renderFence = RendererUtils::createFence();


		// create a descriptor pool
		std::vector<PoolSizeRatio> frameSizes = {
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 },
		};

		frame._frameDescriptors = DescriptorManager{};
		frame._frameDescriptors.init(1000, frameSizes);

		Engine::getDeletionQueue().push_function([&]() {
			frame._frameDescriptors.destroyPools();
		});
	}

	RenderScene::createSceneData();
	RenderScene::setMeshes(AssetManager::getTestMeshes());
}

void Renderer::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
	VkCommandBufferBeginInfo cmdBeginInfo{};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	_drawExtent.height = static_cast<uint32_t>(std::min(Backend::getSwapchainExtent().height, _drawImage.imageExtent.height) * _renderScale);
	_drawExtent.width = static_cast<uint32_t>(std::min(Backend::getSwapchainExtent().width, _drawImage.imageExtent.width) * _renderScale);

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	// Transition to color/depth attachment layouts
	RendererUtils::transitionImage(cmd, _drawImage.image, _drawImage.imageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	if (_currentSampleCount != 1u) {
		RendererUtils::transitionImage(cmd, _msaaImage.image, _msaaImage.imageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	}
	RendererUtils::transitionImage(cmd, _depthImage.image, _depthImage.imageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	// Render scene
	drawGeometry(cmd);

	// Transition draw image to SRC so we can copy to post process
	RendererUtils::transitionImage(cmd, _drawImage.image, _drawImage.imageFormat, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	RendererUtils::transitionImage(cmd, _postProcessImage.image, _postProcessImage.imageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	RendererUtils::copyImageToImage(
		cmd,
		_drawImage.image,
		_postProcessImage.image,
		{ _drawExtent.width, _drawExtent.height },
		{ _drawExtent.width, _drawExtent.height }
	);

	// Transition both back to GENERAL for compute
	RendererUtils::transitionImage(cmd, _drawImage.image, _drawImage.imageFormat, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
	RendererUtils::transitionImage(cmd, _postProcessImage.image, _postProcessImage.imageFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

	// Run compute shader on post-process image
	drawBackground(cmd);

	// Transition post-process image to SRC for copy to swapchain
	RendererUtils::transitionImage(cmd, _postProcessImage.image, _postProcessImage.imageFormat, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	// Transition swapchain image to DST for copy
	RendererUtils::transitionImage(cmd, Backend::getSwapchainImages()[imageIndex], _drawImage.imageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	RendererUtils::copyImageToImage(
		cmd,
		_postProcessImage.image,
		Backend::getSwapchainImages()[imageIndex],
		{ _drawExtent.width, _drawExtent.height },
		Backend::getSwapchainExtent()
	);

	// Transition swapchain to COLOR_ATTACHMENT_OPTIMAL for ImGui
	RendererUtils::transitionImage(cmd, Backend::getSwapchainImages()[imageIndex], _drawImage.imageFormat, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	// Draw ImGui
	EditorImgui::drawImgui(cmd, Backend::getSwapchainImageViews()[imageIndex], false);

	// Transition swapchain to PRESENT
	RendererUtils::transitionImage(cmd, Backend::getSwapchainImages()[imageIndex], _drawImage.imageFormat, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	// END COMMAND BUFFER
	VK_CHECK(vkEndCommandBuffer(cmd));
}

void Renderer::drawGeometry(VkCommandBuffer cmd) {

	//begin a render pass  connected to our draw image
	VkRenderingAttachmentInfo colorAttachment = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.pNext = nullptr,
		.imageView = (_currentSampleCount == 1u) ? _drawImage.imageView : _msaaImage.imageView,
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.resolveMode = (_currentSampleCount == 1u) ? VK_RESOLVE_MODE_NONE : VK_RESOLVE_MODE_AVERAGE_BIT,
		.resolveImageView = (_currentSampleCount == 1u) ? VK_NULL_HANDLE : _drawImage.imageView,
		.resolveImageLayout = (_currentSampleCount == 1u) ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE
	};

	VkRenderingAttachmentInfo depthAttachment = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.pNext = nullptr,
		.imageView = _depthImage.imageView,
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
		.renderArea = { {0, 0}, { _drawExtent.width, _drawExtent.height } },
		.layerCount = 1,
		.viewMask = 0,
		.colorAttachmentCount = 1,
		.pColorAttachments = &colorAttachment,
		.pDepthAttachment = &depthAttachment,
		.pStencilAttachment = nullptr
	};

	vkCmdBeginRendering(cmd, &renderInfo);

	//set dynamic viewport and scissor
	VkViewport viewport = {};
	viewport.x = 0;
	viewport.y = 0;
	viewport.width = static_cast<float>(_drawExtent.width);
	viewport.height = static_cast<float>(_drawExtent.height);
	viewport.minDepth = 0.f;
	viewport.maxDepth = 1.f;

	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor = {};
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	scissor.extent.width = static_cast<uint32_t>(_drawExtent.width);
	scissor.extent.height = static_cast<uint32_t>(_drawExtent.height);

	vkCmdSetScissor(cmd, 0, 1, &scissor);

	RenderScene::renderDrawScene(cmd, getCurrentFrame());

	vkCmdEndRendering(cmd);
}

// requires push constants to render
void Renderer::drawBackground(VkCommandBuffer cmd) {
	// The current shader
	PipelineEffect& effect = Pipelines::drawImagePipeline.getBackgroundEffects();

	VkPipelineLayout& drawImgPipelineLayout = Pipelines::drawImagePipeline.getComputePipelineLayout();

	PushConstantDef& pipelinePCData = Pipelines::drawImagePipeline._pushConstantInfo;

	// bind the background compute pipeline
	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, effect.pipeline);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, drawImgPipelineLayout,
		0, 1, &DescriptorSetOverwatch::getDrawImageDescriptors().descriptorSet, 0, nullptr);

	vkCmdPushConstants(cmd, drawImgPipelineLayout, pipelinePCData.stageFlags, pipelinePCData.offset , pipelinePCData.size, &effect.data);

	// execute the compute pipeline dispatch. We are using 16x16 workgroup size so we need to divide by it
	vkCmdDispatch(cmd, static_cast<uint32_t>(std::ceil(_drawExtent.width / 16.0)), static_cast<uint32_t>(std::ceil(_drawExtent.height / 16.0)), 1);
}

void Renderer::RenderFrame() {
	VkDevice device = Backend::getDevice();
	VkSwapchainKHR swapchain = Backend::getSwapchain();

	auto& frame = getCurrentFrame();

	RenderScene::updateScene();

	VK_CHECK(vkWaitForFences(device, 1, &frame._renderFence, VK_TRUE, UINT64_MAX));

	frame._deletionQueue.flush();
	frame._frameDescriptors.clearPools();

	uint32_t imageIndex;
	VkResult result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, frame._swapchainSemaphore, VK_NULL_HANDLE, &imageIndex);
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
		vkQueueWaitIdle(Backend::getGraphicsQueue());
		Backend::resizeSwapchain();
		return;
	}
	else if (result != VK_SUCCESS) {
		throw std::runtime_error("Failed to present swap chain image!");
	}

	// Only reset the fence if work is submitted
	VK_CHECK(vkResetFences(device, 1, &frame._renderFence));

	VK_CHECK(vkResetCommandBuffer(frame._graphicsCmdBuffer, 0));
	recordCommandBuffer(frame._graphicsCmdBuffer, imageIndex);

	VkCommandBufferSubmitInfo cmdInfo{};
	cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	cmdInfo.pNext = nullptr;
	cmdInfo.commandBuffer = frame._graphicsCmdBuffer;
	cmdInfo.deviceMask = 0;

	VkSemaphoreSubmitInfo waitInfo{};
	waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	waitInfo.pNext = nullptr;
	waitInfo.semaphore = frame._swapchainSemaphore;
	waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR;
	waitInfo.deviceIndex = 0;
	waitInfo.value = 1;

	VkSemaphoreSubmitInfo signalInfo{};
	signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	signalInfo.pNext = nullptr;
	signalInfo.semaphore = frame._renderSemaphore;
	signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
	signalInfo.deviceIndex = 0;
	signalInfo.value = 1;

	VkSubmitInfo2 graphicsSubmitInfo{};
	graphicsSubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	graphicsSubmitInfo.pNext = nullptr;
	graphicsSubmitInfo.waitSemaphoreInfoCount = (waitInfo.semaphore == VK_NULL_HANDLE) ? 0 : 1;
	graphicsSubmitInfo.pWaitSemaphoreInfos = (waitInfo.semaphore == VK_NULL_HANDLE) ? nullptr : &waitInfo;

	graphicsSubmitInfo.signalSemaphoreInfoCount = (signalInfo.semaphore == VK_NULL_HANDLE) ? 0 : 1;
	graphicsSubmitInfo.pSignalSemaphoreInfos = (signalInfo.semaphore == VK_NULL_HANDLE) ? nullptr : &signalInfo;

	graphicsSubmitInfo.commandBufferInfoCount = 1;
	graphicsSubmitInfo.pCommandBufferInfos = &cmdInfo;

	VK_CHECK(vkQueueSubmit2(Backend::getGraphicsQueue(), 1, &graphicsSubmitInfo, frame._renderFence));

	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &frame._renderSemaphore;

	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &swapchain;
	presentInfo.pImageIndices = &imageIndex;

	result = vkQueuePresentKHR(Backend::getPresentQueue(), &presentInfo);
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || Engine::windowModMode().windowResized) {
		if (Backend::getGraphicsQueue() != Backend::getPresentQueue()) {
			vkQueueWaitIdle(Backend::getPresentQueue());
		}
		else {
			vkQueueWaitIdle(Backend::getGraphicsQueue());
		}

		Backend::resizeSwapchain();
	}
	else if (result != VK_SUCCESS) {
		throw std::runtime_error("Failed to present swap chain image!");
	}

	// double buffering, frames indexed at 0 and 1
	_frameNumber++;
}

void Renderer::cleanup() {
	VkDevice device = Backend::getDevice();

	RenderScene::metalRoughMaterial.clearResources(device);

	for (auto& frame : _frames) {
		vkDestroyCommandPool(device, frame._graphicsCmdPool, nullptr);

		vkDestroyFence(device, frame._renderFence, nullptr);
		vkDestroySemaphore(device, frame._renderSemaphore, nullptr);
		vkDestroySemaphore(device, frame._swapchainSemaphore, nullptr);

		frame._deletionQueue.flush();
	}

	_renderImageDeletionQueue.flush();
	vmaDestroyAllocator(_renderImageAllocator);
}