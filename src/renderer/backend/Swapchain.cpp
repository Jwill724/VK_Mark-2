#include "pch.h"

#include "Swapchain.h"
#include "Queue.h"
#include "ImageUtils.h"
#include "EngineTypes.h"

static VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, VkExtent2D winExtent);
static VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
static VkPresentModeKHR ChooseSwapSurfacePresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);

void Swapchain::Create(
	const DeviceContext& deviceCtx,
	VkSurfaceKHR surface,
	const SwapchainSupportDetails& swapchainSupport,
	Extents2D windowExtent)
{
	ASSERT(deviceCtx.device != VK_NULL_HANDLE);

	m_logicalDeviceCopy = deviceCtx.device;

	VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapchainSupport.formats);
	VkPresentModeKHR presentMode = ChooseSwapSurfacePresentMode(swapchainSupport.presentModes);

	VkExtent2D extent = { windowExtent.Width(), windowExtent.Height() };
	VkExtent2D choosenSwapExtent = ChooseSwapExtent(swapchainSupport.capabilities, extent);

	uint32_t imageCount = imageCount = swapchainSupport.capabilities.minImageCount + 1;
	if (swapchainSupport.capabilities.maxImageCount > 0 &&
		imageCount > swapchainSupport.capabilities.maxImageCount)
	{
		imageCount = swapchainSupport.capabilities.maxImageCount;
	}

	if (presentMode == VK_PRESENT_MODE_FIFO_KHR) imageCount = 2; // Hard lock to double buffer

	VkSwapchainCreateInfoKHR createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface = surface;
	createInfo.minImageCount = imageCount;
	createInfo.format = surfaceFormat.format;
	createInfo.imageColorSpace = surfaceFormat.colorSpace;
	createInfo.imageExtent = choosenSwapExtent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	createInfo.preTransform = swapchainSupport.capabilities.currentTransform;
	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	createInfo.presentMode = presentMode;
	createInfo.clipped = VK_TRUE;


	uint32_t qFamIndices[] {
		deviceCtx.queueIndices.graphicsFamily.value(),
		deviceCtx.queueIndices.presentFamily.value()
	};

	if (deviceCtx.queueIndices.graphicsFamily != deviceCtx.queueIndices.presentFamily)
	{
		// Images can be used across multiple queue families without explicit ownership transfer
		createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		createInfo.queueFamilyIndexCount = 2;
		createInfo.pQueueFamilyIndices = qFamIndices;
	}
	else
	{
		// An m_image is owned by one queue family, ownership transfer ship is explicit
		// best performance
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.queueFamilyIndexCount = 0;
		createInfo.pQueueFamilyIndices = nullptr;
	}

	createInfo.oldSwapchain = m_swapchain;

	VK_CHECK(vkCreateSwapchainKHR(m_logicalDeviceCopy, &createInfo, nullptr, &m_swapchain));

	if (createInfo.oldSwapchain != VK_NULL_HANDLE)
	{
		vkDestroySwapchainKHR(m_logicalDeviceCopy, createInfo.oldSwapchain, nullptr);
	}

	vkGetSwapchainImagesKHR(m_logicalDeviceCopy, m_swapchain, &imageCount, nullptr);
	std::vector<VkImage> rawSwapchainImages(imageCount);
	vkGetSwapchainImagesKHR(
		m_logicalDeviceCopy,
		m_swapchain,
		&imageCount,
		rawSwapchainImages.data());

	m_images.resize(imageCount);
	m_views.resize(imageCount);
	m_layouts.resize(imageCount);
	m_imageCount = imageCount;
	m_extent = choosenSwapExtent;
	m_format = surfaceFormat.format;

	for (uint32_t imageIndex = 0; imageIndex < imageCount; ++imageIndex)
	{
		m_images[imageIndex] = std::move(rawSwapchainImages[imageIndex]);
		auto& img = m_images[imageIndex];

		m_views[imageIndex] = ImageUtils::CreateImageView(
			m_logicalDeviceCopy,
			img,
			static_cast<VkFormat>(m_format),
			VK_IMAGE_ASPECT_COLOR_BIT,
			1);
	}

	m_imageAvailableSemaphores.resize(imageCount);
	m_renderFinishedSemaphores.resize(imageCount);
	m_inFlightFences.resize(imageCount);
	m_imageInFlightFrame.resize(imageCount, UINT32_MAX);

	for (uint32_t imgIdx = 0; imgIdx < imageCount; ++imgIdx)
	{
		m_imageAvailableSemaphores[imgIdx] = GPUQueue::CreateNewSemaphore(m_logicalDeviceCopy);
		m_renderFinishedSemaphores[imgIdx] = GPUQueue::CreateNewSemaphore(m_logicalDeviceCopy);
		m_inFlightFences[imgIdx]           = GPUQueue::CreateNewFence(m_logicalDeviceCopy);
	}
}

void Swapchain::ResizeSwapchain(
	const DeviceContext& deviceCtx,
	VkSurfaceKHR surface,
	const SwapchainSupportDetails& swapchainSupport,
	Extents2D windowExtent)
{
	Cleanup();
	Create(deviceCtx, surface, swapchainSupport, windowExtent);
}

void Swapchain::Cleanup()
{
	if (m_swapchain != VK_NULL_HANDLE)
	{
		vkDestroySwapchainKHR(m_logicalDeviceCopy, m_swapchain, nullptr);
		m_swapchain = VK_NULL_HANDLE;
	}

	for (uint32_t i = 0; i < m_imageCount; ++i)
	{
		auto& view = m_views[i];
		ImageUtils::DestroyImageView(m_logicalDeviceCopy, view);

		GPUQueue::DestroyFence(m_logicalDeviceCopy, m_inFlightFences[i]);
		GPUQueue::DestroySemaphore(m_logicalDeviceCopy, m_imageAvailableSemaphores[i]);
		GPUQueue::DestroySemaphore(m_logicalDeviceCopy, m_renderFinishedSemaphores[i]);
	}

	Reset();
}

// ---------------------
// Render swapchain use
// ---------------------

void Swapchain::WaitOnInFlightFence(uint32_t frameIndex) const
{
	VkFence fence = m_inFlightFences[frameIndex];
	VK_CHECK(vkWaitForFences(m_logicalDeviceCopy, 1, &fence, VK_TRUE, UINT64_MAX));
	VK_CHECK(vkResetFences(m_logicalDeviceCopy, 1, &fence));
}

VkResult Swapchain::AcquireNextImage(uint32_t frameIndex) const
{
	uint32_t imageIndex = 0;
	VkResult result = vkAcquireNextImageKHR(
		m_logicalDeviceCopy,
		m_swapchain,
		UINT64_MAX,
		m_imageAvailableSemaphores[frameIndex],
		VK_NULL_HANDLE,
		&imageIndex);

	m_currentSwapchainImageIndex = imageIndex;

	return result;
}

void Swapchain::MarkInFlightFrameIndex(uint32_t frameIndex)
{
	m_imageInFlightFrame[m_currentSwapchainImageIndex] = frameIndex;
}

VkSemaphore Swapchain::GetAvailableSemaphore()
{
	uint32_t currentFrameIndex = m_imageInFlightFrame[m_currentSwapchainImageIndex];
	return m_imageAvailableSemaphores[currentFrameIndex];
}

VkSemaphore Swapchain::GetFinishedSemaphore()
{
	return m_renderFinishedSemaphores[m_currentSwapchainImageIndex];
}

VkFence Swapchain::GetInFlightFence()
{
	uint32_t currentFrameIndex = m_imageInFlightFrame[m_currentSwapchainImageIndex];
	return m_inFlightFences[currentFrameIndex];
}


VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, VkExtent2D winExtent)
{
	if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
	{
		return capabilities.currentExtent;
	}
	else
	{
		VkExtent2D actualExtent {
			winExtent.width,
			winExtent.height
		};
		actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
		actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

		return actualExtent;
	}
}

VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
{
	for (const auto& availableFormat : availableFormats)
	{
		if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			return availableFormat;
	}
	return availableFormats[0];
}

VkPresentModeKHR ChooseSwapSurfacePresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes)
{
	for (const auto& availablePresentMode : availablePresentModes)
	{
		if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
			return availablePresentMode;
	}
	return VK_PRESENT_MODE_FIFO_KHR;
}
