#include "pch.h"

#include "Renderer.h"
#include "renderer/CommandBuffer.h"
#include "vulkan/PipelineManager.h"
#include "Descriptor.h"
#include "imgui/EditorImgui.h"
#include "vulkan/Backend.h"

namespace Renderer {
	unsigned int _frameNumber{ 0 };

	FrameData _frames[MAX_FRAMES_IN_FLIGHT];
	FrameData& getCurrentFrame() { return _frames[_frameNumber % MAX_FRAMES_IN_FLIGHT]; }

	VkExtent3D _drawExtent;
	VkExtent3D getDrawExtent() { return _drawExtent; }

	float _renderScale = 1.f;
	float& getRenderScale() { return _renderScale; }

	AllocatedImage _drawImage;
	AllocatedImage& getDrawImage() { return _drawImage; }
	AllocatedImage _depthImage;
	AllocatedImage& getDepthImage() { return _depthImage; }

	VkImageView& getDrawImageView() { return _drawImage.imageView; }
	DeletionQueue _renderImageDeletionQueue;
	DeletionQueue& getRenderImageDeletionQueue() { return _renderImageDeletionQueue; }
	VmaAllocator _renderImageAllocator;
	VmaAllocator& getRenderImageAllocator() { return _renderImageAllocator; }

	void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex);
	void drawBackground(VkCommandBuffer cmd);
	void drawGeometry(VkCommandBuffer cmd);
}

// SCENE STUFF... coming soon
void Renderer::setMeshes(std::vector<std::shared_ptr<MeshAsset>>& meshes) {
	Scene::_sceneMeshes = meshes;
}

void Renderer::init() {
	QueueFamilyIndices qFamIndices = Backend::getQueueFamilyIndices();

	for (auto& frame : _frames) {
		frame._graphicsCmdPool = CommandBuffer::createCommandPool(qFamIndices.graphicsFamily.value(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
		frame._graphicsCmdBuffer = CommandBuffer::createCommandBuffer(frame._graphicsCmdPool);

		frame._swapchainSemaphore = RendererUtils::createSemaphore();
		frame._renderSemaphore = RendererUtils::createSemaphore();
		frame._renderFence = RendererUtils::createFence();
	}
}

void Renderer::recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
	VkCommandBufferBeginInfo cmdBeginInfo{};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	_drawExtent.height = std::min(Backend::getSwapchainExtent().height, _drawImage.imageExtent.height) * _renderScale;
	_drawExtent.width = std::min(Backend::getSwapchainExtent().width, _drawImage.imageExtent.width) * _renderScale;

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

	// transition our main draw image into general layout so we can write into it
	// we will overwrite it all so we dont care about what was the older layout
	RendererUtils::transitionImage(cmd, _drawImage.image, _drawImage.imageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

	// uses compute shader
	drawBackground(cmd);

	RendererUtils::transitionImage(cmd, _drawImage.image, _drawImage.imageFormat, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	RendererUtils::transitionImage(cmd, _depthImage.image, _depthImage.imageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	drawGeometry(cmd);

	//transition the draw image and the swapchain image into their correct transfer layouts
	RendererUtils::transitionImage(cmd, _drawImage.image, _drawImage.imageFormat, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	RendererUtils::transitionImage(cmd, Backend::getSwapchainImages()[imageIndex], _drawImage.imageFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	// execute a copy from the draw image into the swapchain
	RendererUtils::copyImageToImage(cmd, _drawImage.image, Backend::getSwapchainImages()[imageIndex], { _drawExtent.width, _drawExtent.height }, Backend::getSwapchainExtent());
	// set swapchain image layout to Attachment Optimal so we can draw it
	RendererUtils::transitionImage(cmd, Backend::getSwapchainImages()[imageIndex], _drawImage.imageFormat,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	//draw imgui into the swapchain image
	EditorImgui::drawImgui(cmd, Backend::getSwapchainImageViews()[imageIndex], false);
	// set swapchain image layout to Present so we can show it on the screen
	RendererUtils::transitionImage(cmd, Backend::getSwapchainImages()[imageIndex], _drawImage.imageFormat, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

	VK_CHECK(vkEndCommandBuffer(cmd));
}

void Renderer::drawGeometry(VkCommandBuffer cmd) {

	//begin a render pass  connected to our draw image
	VkRenderingAttachmentInfo colorAttachment = {
		.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
		.pNext = nullptr,
		.imageView = _drawImage.imageView,
		.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		.resolveMode = VK_RESOLVE_MODE_NONE,
		.resolveImageView = VK_NULL_HANDLE,
		.resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.clearValue = {}
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
	depthAttachment.clearValue.depthStencil.depth = 0.f;


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

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipelines::meshPipeline.getPipeline());

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
	scissor.extent.width = static_cast<float>(_drawExtent.width);
	scissor.extent.height = static_cast<float>(_drawExtent.height);

	vkCmdSetScissor(cmd, 0, 1, &scissor);

	GPUDrawPushConstants push_constants;
	push_constants.vertexBuffer = Scene::_sceneMeshes[2]->meshBuffers.vertexBufferAddress;

	PushConstantDef& meshPipelinePCData = Pipelines::meshPipeline._pushConstantInfo;

	float aspect = static_cast<float>(_drawExtent.width) / static_cast<float>(_drawExtent.height);
	AssetManager::transformMesh(push_constants, aspect);

	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipelines::meshPipeline.getPipelineLayout(),
		0, 1, &DescriptorSetOverwatch::getMeshesDescriptors().descriptorSet, 0, nullptr);

	vkCmdPushConstants(cmd, Pipelines::meshPipeline.getPipelineLayout(), meshPipelinePCData.stageFlags, meshPipelinePCData.offset,
		meshPipelinePCData.size, &push_constants);

	vkCmdBindIndexBuffer(cmd, Scene::_sceneMeshes[2]->meshBuffers.indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

	vkCmdDrawIndexed(cmd, Scene::_sceneMeshes[2]->surfaces[0].count, 1, Scene::_sceneMeshes[2]->surfaces[0].startIndex, 0, 0);

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

	VK_CHECK(vkWaitForFences(device, 1, &frame._renderFence, VK_TRUE, UINT64_MAX));

	frame._deletionQueue.flush();

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

	_frameNumber++;
}

void Renderer::cleanup() {
	VkDevice device = Backend::getDevice();

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