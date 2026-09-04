#include "pch.h"

#include "PhysicalDeviceSelector.h"
#include "VulkanTypes.h"
#include <set>
#include <string>

static std::optional<PhysicalDeviceCandidate> EvaluateDevice(VkPhysicalDevice pDevice, VkSurfaceKHR surface, const std::vector<const char*>& deviceExtensions);
static bool HasRequiredQueues(VkPhysicalDevice pDevice, VkSurfaceKHR surface);
static bool CheckDeviceExtensionSupport(VkPhysicalDevice pDevice, const std::vector<const char*> deviceExtensions);
static SwapchainSupportDetails QuerySwapchainSupport(VkPhysicalDevice pDevice, VkSurfaceKHR surface);
static QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice pDevice, VkSurfaceKHR surface);
static bool QueryRayTracingSupport(VkPhysicalDevice pDevice);
static bool QueryDeviceFaultSupport(VkPhysicalDevice pDevice);

PhysicalDeviceCandidate PhysicalDeviceSelector::PickBest(
	VkInstance instance,
	VkSurfaceKHR surface,
	const std::vector<const char*> deviceExtensions)
{
	ASSERT(instance != VK_NULL_HANDLE);
	ASSERT(surface != VK_NULL_HANDLE);

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

	if (!QueryRayTracingSupport(pDevice))
		return std::nullopt;

	if (!QueryDeviceFaultSupport(pDevice))
		return std::nullopt;

	PhysicalDeviceCandidate candidate{};
	candidate.pDevice          = pDevice;
	candidate.queueIndices     = FindQueueFamilies(pDevice, surface);
	candidate.swapchainSupport = std::move(swapchainSupport);
	candidate.props.Query(pDevice);
	candidate.name             = std::string(candidate.props.core.properties.deviceName);

	REQUIRE_HARDWARE(
		candidate.props.limits.maxPushConstantsSize >= RD::MAX_PUSH_CONSTANT_SIZE,
		"Device should support push constant sizing up to 256 bytes!");

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

static bool QueryRayTracingSupport(VkPhysicalDevice pDevice)
{
	VkPhysicalDeviceAccelerationStructureFeaturesKHR as{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
	VkPhysicalDeviceRayQueryFeaturesKHR rq{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
	VkPhysicalDeviceRayTracingPositionFetchFeaturesKHR pf{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_POSITION_FETCH_FEATURES_KHR };
	VkPhysicalDeviceMeshShaderFeaturesEXT ms{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT };

	VkPhysicalDeviceFeatures2 feats{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	feats.pNext = &as;
	as.pNext    = &rq;
	rq.pNext    = &pf;
	pf.pNext    = &ms;

	vkGetPhysicalDeviceFeatures2(pDevice, &feats);

	REQUIRE_HARDWARE(
		as.accelerationStructure &&
		as.descriptorBindingAccelerationStructureUpdateAfterBind &&
		rq.rayQuery &&
		pf.rayTracingPositionFetch &&
		ms.meshShader && ms.taskShader,
		"Requires ray query, position fetch, and mesh shaders — RTX 20 series and up");

	return true;
}

static bool QueryDeviceFaultSupport(VkPhysicalDevice pDevice)
{
	VkPhysicalDeviceFaultFeaturesEXT fault{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT };

	VkPhysicalDeviceFeatures2 feats{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	feats.pNext = &fault;

	vkGetPhysicalDeviceFeatures2(pDevice, &feats);

	REQUIRE_HARDWARE(fault.deviceFault, "Requires VK_EXT_device_fault");

	return true;
}
