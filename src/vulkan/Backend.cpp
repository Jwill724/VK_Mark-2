#include "pch.h"

#include "Backend.h"
#include "core/ResourceManager.h"
#include "Window.h"
#include "utils/RendererUtils.h"
#include "Engine.h"

namespace Backend {
	VkInstance _instance = VK_NULL_HANDLE;
	VkInstance getInstance() { return _instance; }

	static VkPhysicalDeviceProperties _deviceProps{};
	static VkPhysicalDeviceLimits _deviceLimits{};

	const VkPhysicalDeviceLimits getDeviceLimits() {
		return _deviceLimits;
	}

	VkDebugUtilsMessengerEXT _debugMessenger = VK_NULL_HANDLE;

	VkSurfaceKHR _surface = VK_NULL_HANDLE;
	VkSurfaceKHR getSurface() { return _surface; }

	VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
	VkPhysicalDevice getPhysicalDevice() { return _physicalDevice; }

	VkDevice _device = VK_NULL_HANDLE;
	VkDevice getDevice() { return _device; }

	QueueFamilyIndices _queueFamilyIndices;
	QueueFamilyIndices getQueueFamilyIndices() { return _queueFamilyIndices; }

	GPUQueue _graphicsQueue{};
	GPUQueue& getGraphicsQueue() { return _graphicsQueue; }

	GPUQueue _presentQueue{};
	GPUQueue& getPresentQueue() { return _presentQueue; }

	GPUQueue _transferQueue{};
	GPUQueue& getTransferQueue() { return _transferQueue; }

	GPUQueue _computeQueue{};
	GPUQueue& getComputeQueue() { return _computeQueue; }

	SwapchainDef _swapchainDef;
	SwapchainDef& getSwapchainDef() { return _swapchainDef; }

	void createInstance();
	void createSurface();
	void pickPhysicalDevice();
	void createLogicalDevice();
	void createSwapchain();
	void cleanupSwapchain();
	void createImageViews();
	VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
}

void Backend::initVulkanCore() {
	createInstance();
	BackendTools::setupDebugMessenger(_instance, _debugMessenger);
	createSurface();
	pickPhysicalDevice();
	createLogicalDevice();
	createSwapchain();
	createImageViews();
}

void Backend::createInstance() {
	if (BackendTools::enableValidationLayers) {
		if (!BackendTools::checkValidationLayerSupport()) {
			fmt::print("Validation layers requested, but not available!\n");
		}
	}

	VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Mk2";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 3, 0);
	appInfo.pEngineName = "Engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_3;

	VkInstanceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;

	auto reqExtensions = BackendTools::getRequiredExtensions();
	createInfo.enabledExtensionCount = static_cast<uint32_t>(reqExtensions.size());
	createInfo.ppEnabledExtensionNames = reqExtensions.data();

	if (BackendTools::enableValidationLayers) {
		createInfo.enabledLayerCount = static_cast<uint32_t>(BackendTools::validationLayers.size());
		createInfo.ppEnabledLayerNames = BackendTools::validationLayers.data();

		VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
		BackendTools::populateDebugMessengerCreateInfo(debugCreateInfo);

		if (BackendTools::enableGPUValidationLayers) {
			static VkValidationFeatureEnableEXT gpuEnableFeatures[] = {
				VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
				VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT
			};

			static VkValidationFeaturesEXT gpuValidationFeatures{};
			gpuValidationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
			gpuValidationFeatures.enabledValidationFeatureCount = 2;
			gpuValidationFeatures.pEnabledValidationFeatures = gpuEnableFeatures;
			gpuValidationFeatures.pNext = &debugCreateInfo;

			createInfo.pNext = &gpuValidationFeatures;
		}
		else {
			createInfo.pNext = &debugCreateInfo;
		}
	}
	else {
		createInfo.enabledLayerCount = 0;
		createInfo.pNext = nullptr;
	}

	VK_CHECK(vkCreateInstance(&createInfo, nullptr, &_instance));
}

void Backend::createSurface() {
	VK_CHECK(glfwCreateWindowSurface(_instance, Engine::getWindow(), nullptr, &_surface));
}

void Backend::pickPhysicalDevice() {
	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(_instance, &deviceCount, nullptr);

	ASSERT(deviceCount != 0 && "[Backend] No physical device found.\n");

	std::vector<VkPhysicalDevice> devices(deviceCount);

	vkEnumeratePhysicalDevices(_instance, &deviceCount, devices.data());
	for (const auto& device : devices) {
		if (BackendTools::isDeviceSuitable(device, _surface)) {
			_physicalDevice = device;
			fmt::print("Physical device {}\n", (void*)_physicalDevice);
			_queueFamilyIndices = VulkanUtils::FindQueueFamilies(_physicalDevice, _surface);
			break;
		}
	}
	ASSERT(_physicalDevice != VK_NULL_HANDLE);

	vkGetPhysicalDeviceProperties(_physicalDevice, &_deviceProps);
	_deviceLimits = _deviceProps.limits;
	ResourceManager::getAvailableSampleCounts() = VulkanUtils::findSupportedSampleCounts(_deviceLimits);
}

void Backend::createLogicalDevice() {
	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
	std::set<uint32_t> uniqueQueueFamilies;

	// Only add queue families that exist
	if (_queueFamilyIndices.graphicsFamily.has_value()) {
		uniqueQueueFamilies.insert(_queueFamilyIndices.graphicsFamily.value());
		_graphicsQueue.familyIndex = _queueFamilyIndices.graphicsFamily.value();
	}
	if (_queueFamilyIndices.presentFamily.has_value()) {
		uniqueQueueFamilies.insert(_queueFamilyIndices.presentFamily.value());
		_presentQueue.familyIndex = _queueFamilyIndices.presentFamily.value();
	}
	if (_queueFamilyIndices.transferFamily.has_value()) {
		uniqueQueueFamilies.insert(_queueFamilyIndices.transferFamily.value());
		_transferQueue.familyIndex = _queueFamilyIndices.transferFamily.value();
	}
	if (_queueFamilyIndices.computeFamily.has_value()) {
		uniqueQueueFamilies.insert(_queueFamilyIndices.computeFamily.value());
		_computeQueue.familyIndex = _queueFamilyIndices.computeFamily.value();
	}

	float queuePriority = 1.0f;
	for (uint32_t queueFamily : uniqueQueueFamilies) {
		VkDeviceQueueCreateInfo queueCreateInfo{};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = queueFamily;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;
		queueCreateInfos.push_back(queueCreateInfo);
	}

	VkPhysicalDeviceFeatures2 baseFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	baseFeatures.features.fillModeNonSolid = VK_TRUE;
	baseFeatures.features.samplerAnisotropy = VK_TRUE;
	baseFeatures.features.multiDrawIndirect = VK_TRUE;				// indirect draws enabled
	baseFeatures.features.shaderInt64 = VK_TRUE;					// 64-bit addressing
	baseFeatures.features.tessellationShader = VK_TRUE;
	baseFeatures.features.depthBiasClamp = VK_TRUE;
	baseFeatures.features.drawIndirectFirstInstance = VK_TRUE;
	baseFeatures.features.imageCubeArray = VK_TRUE;
	baseFeatures.features.occlusionQueryPrecise = VK_TRUE;

	VkPhysicalDeviceVulkan11Features features11{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
	features11.shaderDrawParameters = VK_TRUE;						// InstanceIndex

	VkPhysicalDeviceVulkan12Features features12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	features12.bufferDeviceAddress = VK_TRUE;						// GPU pointers
	features12.descriptorIndexing = VK_TRUE;						// Enables all indexing stuff
	features12.timelineSemaphore = VK_TRUE;							// Timeline sync (async GPU workloads)
	features12.scalarBlockLayout = VK_TRUE;							// No descriptor padding
	//features12.shaderBufferInt64Atomics = VK_TRUE;
	features12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
	features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
	features12.descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
	features12.descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
	features12.descriptorBindingUniformBufferUpdateAfterBind = VK_TRUE;
	features12.descriptorBindingPartiallyBound = VK_TRUE;
	features12.descriptorBindingVariableDescriptorCount = VK_TRUE;
	features12.runtimeDescriptorArray = VK_TRUE;
	features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
	features12.shaderStorageImageArrayNonUniformIndexing = VK_TRUE;

	VkPhysicalDeviceVulkan13Features features13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
	features13.dynamicRendering = VK_TRUE;
	features13.synchronization2 = VK_TRUE;
	features13.maintenance4 = VK_TRUE;

	features13.pNext = nullptr;
	features12.pNext = &features13;
	features11.pNext = &features12;
	baseFeatures.pNext = &features11;

	VkDeviceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.pNext = &baseFeatures;
	createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
	createInfo.pQueueCreateInfos = queueCreateInfos.data();
	createInfo.enabledExtensionCount = static_cast<uint32_t>(BackendTools::deviceExtensions.size());
	createInfo.ppEnabledExtensionNames = BackendTools::deviceExtensions.data();
	createInfo.enabledLayerCount = BackendTools::enableValidationLayers ? static_cast<uint32_t>(BackendTools::validationLayers.size()) : 0;
	createInfo.ppEnabledLayerNames = BackendTools::enableValidationLayers ? BackendTools::validationLayers.data() : nullptr;

	VK_CHECK(vkCreateDevice(_physicalDevice, &createInfo, nullptr, &_device));

	// Retrieve queues only if they were created
	if (_queueFamilyIndices.graphicsFamily.has_value()) {
		vkGetDeviceQueue(_device, _graphicsQueue.familyIndex, 0, &_graphicsQueue.queue);
		_graphicsQueue.fencePool.device = _device;
		_graphicsQueue.qType = QueueType::Graphics;
	}

	if (_queueFamilyIndices.presentFamily.has_value()) {
		vkGetDeviceQueue(_device, _presentQueue.familyIndex, 0, &_presentQueue.queue);
		_presentQueue.fencePool.device = _device;
		_presentQueue.qType = QueueType::Present;
	}

	if (_queueFamilyIndices.transferFamily.has_value()) {
		vkGetDeviceQueue(_device, _transferQueue.familyIndex, 0, &_transferQueue.queue);
		_transferQueue.fencePool.device = _device;
		_transferQueue.qType = QueueType::Transfer;
	}

	if (_queueFamilyIndices.computeFamily.has_value()) {
		vkGetDeviceQueue(_device, _computeQueue.familyIndex, 0, &_computeQueue.queue);
		_computeQueue.fencePool.device = _device;
		_computeQueue.qType = QueueType::Compute;
	}
}

void Backend::createSwapchain() {
	BackendTools::SwapChainSupportDetails swapChainSupport = BackendTools::querySwapChainSupport(_physicalDevice, _surface);
	VkSurfaceFormatKHR surfaceFormat = BackendTools::chooseSwapSurfaceFormat(swapChainSupport.formats);
	//VkPresentModeKHR presentMode = BackendTools::chooseSwapSurfacePresentMode(swapChainSupport.presentModes);
	VkPresentModeKHR VSYNC = VK_PRESENT_MODE_FIFO_KHR;
	VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

	uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
	if (swapChainSupport.capabilities.maxImageCount > 0 &&
		imageCount > swapChainSupport.capabilities.maxImageCount) {
		imageCount = swapChainSupport.capabilities.maxImageCount;
	}

	VkSwapchainCreateInfoKHR createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface = _surface;
	createInfo.minImageCount = imageCount;
	createInfo.imageFormat = surfaceFormat.format;
	createInfo.imageColorSpace = surfaceFormat.colorSpace;
	createInfo.imageExtent = extent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	createInfo.presentMode = VSYNC;
	createInfo.clipped = VK_TRUE;

	uint32_t qFamIndices[] = {
		_queueFamilyIndices.graphicsFamily.value(),
		_queueFamilyIndices.presentFamily.value()
	};

	if (_queueFamilyIndices.graphicsFamily != _queueFamilyIndices.presentFamily) {
		// Images can be used across multiple queue families without explicit ownership transfer
		createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		createInfo.queueFamilyIndexCount = 2;
		createInfo.pQueueFamilyIndices = qFamIndices;
	}
	else {
		// An image is owned by one queue family, ownership transfer ship is explicit
		// best performance
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.queueFamilyIndexCount = 0;
		createInfo.pQueueFamilyIndices = nullptr;
	}

	createInfo.oldSwapchain = _swapchainDef.swapchain;

	VK_CHECK(vkCreateSwapchainKHR(_device, &createInfo, nullptr, &_swapchainDef.swapchain));

	if (createInfo.oldSwapchain != VK_NULL_HANDLE) {
		vkDestroySwapchainKHR(_device, createInfo.oldSwapchain, nullptr);
	}

	vkGetSwapchainImagesKHR(_device, _swapchainDef.swapchain, &imageCount, nullptr);
	_swapchainDef.images.resize(imageCount);
	vkGetSwapchainImagesKHR(_device, _swapchainDef.swapchain, &imageCount, _swapchainDef.images.data());

	_swapchainDef.imageFormat = surfaceFormat.format;
	_swapchainDef.extent = extent;
}

void Backend::resizeSwapchain() {
	cleanupSwapchain();

	Engine::windowModMode().updateWindowSize();

	createSwapchain();
	createImageViews();
}

void Backend::cleanupSwapchain() {
	// do not ever touch this in any way or it breaks
	if (_swapchainDef.swapchain != VK_NULL_HANDLE) {
		vkDestroySwapchainKHR(_device, _swapchainDef.swapchain, nullptr);
		_swapchainDef.swapchain = VK_NULL_HANDLE;
	}

	for (size_t i = 0; i < _swapchainDef.imageViews.size(); ++i) {
		vkDestroyImageView(_device, _swapchainDef.imageViews[i], nullptr);
	}
}

VkExtent2D Backend::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
	if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
		return capabilities.currentExtent;
	}
	else {
		int width, height;
		glfwGetFramebufferSize(Engine::getWindow(), &width, &height);

		VkExtent2D actualExtent = {
			static_cast<uint32_t>(width),
			static_cast<uint32_t>(height)
		};
		actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
		actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

		return actualExtent;
	}
}

void Backend::createImageViews() {
	_swapchainDef.imageViews.resize(_swapchainDef.images.size());

	for (uint32_t i = 0; i < _swapchainDef.images.size(); ++i) {
		_swapchainDef.imageViews[i] = RendererUtils::createImageView(
			_device,
			_swapchainDef.images[i],
			_swapchainDef.imageFormat,
			VK_IMAGE_ASPECT_COLOR_BIT,
			1
		);
	}
}

void Backend::cleanupBackend() {
	_graphicsQueue.fencePool.destroyFences();
	_presentQueue.fencePool.destroyFences();
	_transferQueue.fencePool.destroyFences();
	_computeQueue.fencePool.destroyFences();

	cleanupSwapchain();

	vkDestroySurfaceKHR(_instance, _surface, nullptr);

	vkDestroyDevice(_device, nullptr);

	if (BackendTools::enableValidationLayers) {
		BackendTools::DestroyDebugUtilsMessengerEXT(_instance, _debugMessenger, nullptr);
	}
	vkDestroyInstance(_instance, nullptr);
}