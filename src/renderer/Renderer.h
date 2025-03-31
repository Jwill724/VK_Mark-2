#pragma once

#include <common/Vk_Types.h>
#include "utils/RendererUtils.h"
#include "core/AssetManager.h"

constexpr unsigned int MAX_FRAMES_IN_FLIGHT = 2;

struct FrameData {
	VkCommandPool _graphicsCmdPool = VK_NULL_HANDLE;
	VkCommandBuffer _graphicsCmdBuffer = VK_NULL_HANDLE;
	VkSemaphore _swapchainSemaphore = VK_NULL_HANDLE;
	VkSemaphore _renderSemaphore = VK_NULL_HANDLE;
	VkFence _renderFence = VK_NULL_HANDLE;
	DeletionQueue _deletionQueue;
};

namespace Renderer {
	FrameData& getCurrentFrame();
	VkExtent3D getDrawExtent();
	AllocatedImage& getDrawImage();
	AllocatedImage& getDepthImage();
	VkImageView& getDrawImageView();
	DeletionQueue& getRenderImageDeletionQueue();
	VmaAllocator& getRenderImageAllocator();

	void init();
	void setMeshes(std::vector<std::shared_ptr<MeshAsset>>& meshes);
	float& getRenderScale();

	void RenderFrame();
	void cleanup();
}

// RenderScene file?
namespace Scene {
	inline std::vector<std::shared_ptr<MeshAsset>> _sceneMeshes;

//	void cleanScene();
}