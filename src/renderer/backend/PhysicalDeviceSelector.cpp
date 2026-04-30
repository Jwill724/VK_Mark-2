#include "pch.h"

#include "PhysicalDeviceSelector.h"
#include "Core.h"
#include <set>
#include <string>

static std::optional<PhysicalDeviceCandidate> EvaluateDevice(VkPhysicalDevice pDevice, VkSurfaceKHR surface, const std::vector<const char*>& deviceExtensions);
static bool HasRequiredQueues(VkPhysicalDevice pDevice, VkSurfaceKHR surface);
static bool CheckDeviceExtensionSupport(VkPhysicalDevice pDevice, const std::vector<const char*> deviceExtensions);
static SwapchainSupportDetails QuerySwapchainSupport(VkPhysicalDevice pDevice, VkSurfaceKHR surface);
static QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice pDevice, VkSurfaceKHR surface);

PhysicalDeviceCandidate PhysicalDeviceSelector::PickBest(
	VkInstance instance,
	VkSurfaceKHR surface,
	const std::vector<const char*> deviceExtensions)
{
	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

	ASSERT(deviceCount != 0 && "No physical device found.");

	std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
	vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data());

	for (const auto& pDevice : physicalDevices)
	{
		auto candidate = EvaluateDevice(pDevice, surface, deviceExtensions);

		if (candidate.has_value())
		{
			return candidate.value();
		}
	}

	ASSERT(false && "No suitable device found.");
	return {};
}

std::optional<PhysicalDeviceCandidate> EvaluateDevice(
	VkPhysicalDevice pDevice,
	VkSurfaceKHR surface,
	const std::vector<const char*>& deviceExtensions)
{
	if (!HasRequiredQueues(pDevice, surface))
		return std::nullopt;

	if (!CheckDeviceExtensionSupport(pDevice, deviceExtensions))
		return std::nullopt;

	SwapchainSupportDetails swapchainSupport = QuerySwapchainSupport(pDevice, surface);

	if (swapchainSupport.formats.empty() || swapchainSupport.presentModes.empty())
		return std::nullopt;

	VkPhysicalDeviceFeatures supportedFeatures;
	vkGetPhysicalDeviceFeatures(pDevice, &supportedFeatures);

	if (!(supportedFeatures.drawIndirectFirstInstance &&
		  supportedFeatures.multiDrawIndirect &&
		  supportedFeatures.fullDrawIndexUint32 &&
		  supportedFeatures.shaderInt16 &&
		  supportedFeatures.imageCubeArray &&
		  supportedFeatures.shaderInt64 &&
		  supportedFeatures.depthBiasClamp &&
		  supportedFeatures.depthClamp &&
		  supportedFeatures.samplerAnisotropy))
	{
		return std::nullopt;
	}

	PhysicalDeviceCandidate candidate{};
	candidate.pDevice = pDevice;
	candidate.queueIndices = FindQueueFamilies(pDevice, surface);
	candidate.swapchainSupport = std::move(swapchainSupport);

	vkGetPhysicalDeviceProperties(pDevice, &candidate.properties);
	candidate.limits = candidate.properties.limits;
	candidate.name = std::string(candidate.properties.deviceName);

	return candidate;
}

QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice pDevice, VkSurfaceKHR surface)
{
	QueueFamilyIndices indices;

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(pDevice, &queueFamilyCount, nullptr);

	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(pDevice, &queueFamilyCount, queueFamilies.data());

	int i = 0;
	for (const auto& queueFamily : queueFamilies)
	{
		if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			indices.graphicsFamily = i;
		}

		VkBool32 presentSupport = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(pDevice, i, surface, &presentSupport);
		if (presentSupport) {
			indices.presentFamily = i;
		}

		// Find dedicated transfer queue
		if ((queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) &&
			!(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
			!indices.transferFamily.has_value())
		{
			indices.transferFamily = i;
			//fmt::println("Found index {}, dedicated transfer queue !", i);
		}

		// Find dedicated compute queue
		if ((queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) &&
			!(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
			!indices.computeFamily.has_value())
		{
			indices.computeFamily = i;
			//fmt::println("Found index {}, dedicated compute queue!", i);
		}

		if (indices.IsComplete()) { break; }

		i++;
	}

	if (!indices.transferFamily.has_value() && indices.graphicsFamily.has_value())
	{
		fmt::println("No dedicated transfer queue, falling back to graphics queue.");
		indices.transferFamily = indices.graphicsFamily;
	}

	if (!indices.computeFamily.has_value() && indices.graphicsFamily.has_value())
	{
		fmt::println("No dedicated compute queue, falling back to graphics queue.");
		indices.computeFamily = indices.graphicsFamily;
	}

	return indices;
}


bool HasRequiredQueues(VkPhysicalDevice pDevice, VkSurfaceKHR surface)
{
	QueueFamilyIndices indices;

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(pDevice, &queueFamilyCount, nullptr);
	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(pDevice, &queueFamilyCount, queueFamilies.data());

	int i = 0;
	for (const auto& queueFamily : queueFamilies)
	{
		if (queueFamily.queueCount > 0 && queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
		{
			indices.graphicsFamily = i;
		}

		VkBool32 presentSupport = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(pDevice, i, surface, &presentSupport);
		if (presentSupport) { indices.presentFamily = i; }

		if (indices.graphicsFamily.has_value() && indices.presentFamily.has_value()) return true;

		i++;
	}

	return false;
}

bool CheckDeviceExtensionSupport(VkPhysicalDevice pDevice, const std::vector<const char*> deviceExtensions)
{
	uint32_t extensionCount;
	vkEnumerateDeviceExtensionProperties(pDevice, nullptr, &extensionCount, nullptr);

	std::vector<VkExtensionProperties> availableExtensions(extensionCount);
	vkEnumerateDeviceExtensionProperties(pDevice, nullptr, &extensionCount, availableExtensions.data());

	std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

	for (const auto& extension : availableExtensions)
		requiredExtensions.erase(extension.extensionName);

	return requiredExtensions.empty();
}

SwapchainSupportDetails QuerySwapchainSupport(VkPhysicalDevice pDevice, VkSurfaceKHR surface)
{
	SwapchainSupportDetails details;

	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pDevice, surface, &details.capabilities);

	uint32_t formatCount;
	vkGetPhysicalDeviceSurfaceFormatsKHR(pDevice, surface, &formatCount, nullptr);
	if (formatCount != 0)
	{
		details.formats.resize(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(pDevice, surface, &formatCount, details.formats.data());
	}

	uint32_t presentModeCount;
	vkGetPhysicalDeviceSurfacePresentModesKHR(pDevice, surface, &presentModeCount, nullptr);
	if (presentModeCount != 0)
	{
		details.presentModes.resize(presentModeCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(pDevice, surface, &presentModeCount, details.presentModes.data());
	}

	return details;
}
