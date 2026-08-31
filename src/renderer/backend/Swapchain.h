#pragma once

#include "VulkanTypes.h"

struct Extents2D;

// TODO: Add vsync toggling

class Swapchain final
{
public:
	void Init(const DeviceContext& deviceCtx,
		VkSurfaceKHR surface,
		const SwapchainSupportDetails& swapchainSupport,
		Extents2D windowExtent)
	{
		Create(deviceCtx, surface, swapchainSupport, windowExtent);
	}

	void ResizeSwapchain(
		const DeviceContext& deviceCtx,
		VkSurfaceKHR surface,
		const SwapchainSupportDetails& swapchainSupport,
		Extents2D windowExtent);

	// Called during frame context prepartion
	VkResult WaitOnInFlightFence(uint32_t frameIndex) const;
	VkResult AcquireNextImage(uint32_t frameIndex) const;
	void MarkInFlightFrameIndex(uint32_t frameIndex);

	// Called at start of frame submit
	VkSemaphore GetAvailableSemaphore();
	VkSemaphore GetFinishedSemaphore();
	VkFence GetInFlightFence();

	uint32_t GetImageCount() const { return m_imageCount; }

	void Cleanup();

	uint32_t GetCurrentSwapchainImageIndex() const { return m_currentSwapchainImageIndex; }
	VkSwapchainKHR GetSwapchainHandle() const { return m_swapchain; }

	VkImage GetCurrentImage() const { return m_images[m_currentSwapchainImageIndex]; };
	VkImageView GetCurrentView() const { return m_views[m_currentSwapchainImageIndex]; };

	VkExtent2D GetExtent() const { return m_extent; }

	VkFormat GetFormat() const { return m_format; }

private:
	void Create(
		const DeviceContext& deviceCtx,
		VkSurfaceKHR surface,
		const SwapchainSupportDetails& swapchainSupport,
		Extents2D windowExtent);

	VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
	std::vector<VkImage> m_images;
	std::vector<VkImageView> m_views;
	std::vector<Vulkan_ImageLayout> m_layouts;

	uint32_t m_imageCount = UINT32_MAX;

	VkDevice m_logicalDeviceCopy = VK_NULL_HANDLE;
	mutable uint32_t m_currentSwapchainImageIndex = 0;

	VkSurfaceFormatKHR m_surfaceFormat{};
	VkPresentModeKHR m_presentMode{};
	VkExtent2D m_extent{};
	VkFormat m_format = VK_FORMAT_MAX_ENUM;

	std::vector<VkSemaphore> m_imageAvailableSemaphores{};
	std::vector<VkSemaphore> m_renderFinishedSemaphores{};
	std::vector<VkFence> m_inFlightFences{};
	std::vector<uint32_t> m_imageInFlightFrame{};

	void Reset()
	{
		m_views.clear();
		m_layouts.clear();
		m_imageAvailableSemaphores.clear();
		m_renderFinishedSemaphores.clear();
		m_inFlightFences.clear();
		m_imageInFlightFrame.clear();
		m_imageCount = 0;
	}
};
