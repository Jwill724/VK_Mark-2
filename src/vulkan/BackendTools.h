#pragma once

#include "common/Vk_Types.h"
#include "cstring"
#include "set"

struct SwapchainDef {
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	std::vector<VkImage> images{};
	std::vector<VkImageView> imageViews{};
	VkFormat imageFormat{};
	VkExtent2D extent{};
	uint32_t imageCount = UINT32_MAX;

	std::vector<VkSemaphore> presentSemaphores;
	std::vector<VkSemaphore> renderFinishedSemaphores;
	std::vector<VkFence> inFlightFences;
	std::vector<uint32_t> imageInFlightFrame;
};

namespace BackendTools {
	const std::vector<const char*> validationLayers {
		"VK_LAYER_KHRONOS_validation"
	};
	const std::vector<const char*> deviceExtensions {
		VK_KHR_SWAPCHAIN_EXTENSION_NAME
	};

#ifdef NDEBUG
	inline const bool enableValidationLayers = false;
	inline const bool enableGPUValidationLayers = false;
#else
	inline const bool enableValidationLayers = true;
	inline const bool enableGPUValidationLayers = false;
#endif

	// Swap chain controls how GPU renders images
	// Formats (Color depth, pixel format, etc)
	// PresentModes (frame buffering, vsync settings, etc);
	// Capabilities (Res, min/max image bounds, etc);
	struct SwapChainSupportDetails {
		VkSurfaceCapabilitiesKHR capabilities{};
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> presentModes;
	};

	static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
		VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT messageType,
		const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
		void* pUserData);
	static VkResult CreateDebugUtilsMessengerEXT(VkInstance instance,
		const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
		const VkAllocationCallbacks* pAllocator,
		VkDebugUtilsMessengerEXT* pDebugMessenger);
	void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator);
	void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);
	SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);
	VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
	VkPresentModeKHR chooseSwapSurfacePresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
	void setupDebugMessenger(VkInstance instance, VkDebugUtilsMessengerEXT& debugMessenger);
	std::vector<const char*> getRequiredExtensions();
	bool checkValidationLayerSupport();
	bool checkDeviceExtensionSupport(VkPhysicalDevice device);
	bool isDeviceSuitable(VkPhysicalDevice device, VkSurfaceKHR surface);
}