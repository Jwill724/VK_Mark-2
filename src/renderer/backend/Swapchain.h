#pragma once

#include "VulkanTypes.h"
#include "memory/AllocatedImage.h"

class Swapchain final
{
public:
	Swapchain() = default;
	Swapchain(
		const DeviceContext& deviceCtx,
		VkSurfaceKHR surface,
		const QueueFamilyIndices& queueFamilyIndices,
		const SwapchainSupportDetails& swapchainSupport,
		const std::array<uint32_t, 2>& windowExtent)
	{
		Create(deviceCtx, surface, queueFamilyIndices, swapchainSupport, windowExtent);
	}

	void ResizeSwapchain(
		const DeviceContext& deviceCtx,
		VkSurfaceKHR surface,
		const QueueFamilyIndices& queueFamilyIndices,
		const SwapchainSupportDetails& swapchainSupport,
		const std::array<uint32_t, 2>& windowExtent);

	// Called during frame context prepartion
	void WaitOnInFlightFence(uint32_t frameIndex) const;
	VkResult AcquireNextImage(uint32_t frameIndex) const;
	void MarkInFlightFrameIndex(uint32_t frameIndex);

	// Called at start of frame submit
	VkSemaphore GetAvailableSemaphore();
	VkSemaphore GetFinishedSemaphore();
	VkFence GetInFlightFence();

	void Cleanup();

private:
	void Create(
		const DeviceContext& deviceCtx,
		VkSurfaceKHR surface,
		const QueueFamilyIndices& queueFamilyIndices,
		const SwapchainSupportDetails& swapchainSupport,
		const std::array<uint32_t, 2>& windowExtent);

	VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
	std::vector<AllocatedImage> m_images{};
	uint32_t m_imageCount = UINT32_MAX;

	VkDevice m_logicalDeviceCopy;
	mutable uint32_t m_currentSwapchainImageIndex = 0;

	VkSurfaceFormatKHR m_surfaceFormat{};
	VkPresentModeKHR m_presentMode{};
	VkExtent2D m_extent{};
	VkFormat m_format = VK_FORMAT_MAX_ENUM;

	std::vector<VkSemaphore> m_imageAvailableSemaphores{};
	std::vector<VkSemaphore> m_renderFinishedSemaphores{};
	std::vector<VkFence> m_inFlightFences{};
	std::vector<uint32_t> m_imageInFlightFrame{};
};
