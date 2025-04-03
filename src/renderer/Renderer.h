#pragma once

#include <common/Vk_Types.h>
#include "utils/RendererUtils.h"
#include "core/AssetManager.h"
#include "renderer/Descriptor.h"
#include "renderer/CommandBuffer.h"

constexpr unsigned int MAX_FRAMES_IN_FLIGHT = 2;

struct FrameData {
	VkCommandPool _graphicsCmdPool = VK_NULL_HANDLE;
	VkCommandBuffer _graphicsCmdBuffer = VK_NULL_HANDLE;
	VkSemaphore _swapchainSemaphore = VK_NULL_HANDLE;
	VkSemaphore _renderSemaphore = VK_NULL_HANDLE;
	VkFence _renderFence = VK_NULL_HANDLE;
	DeletionQueue _deletionQueue;
	DescriptorManager _frameDescriptors;
};

namespace Renderer {
	AllocatedImage& getDrawImage();
	AllocatedImage& getDepthImage();
	FrameData& getCurrentFrame();
	VkExtent3D getDrawExtent();
	VkImageView& getDrawImageView();
	DeletionQueue& getRenderImageDeletionQueue();
	VmaAllocator& getRenderImageAllocator();
	float& getRenderScale();
	AllocatedImage& getPostProcessImage();

//	uint32_t getRenderImagesSampleCount();
//	uint32_t getCurrentMSAAIndex();

	uint32_t getCurrentSampleCount();
	std::vector<VkSampleCountFlags>& getAvailableSampleCounts();

	extern VkDescriptorSetLayout _drawImageDescriptorLayout;

	void init();
	void setupRenderImages();

	void RenderFrame();
	void cleanup();
}