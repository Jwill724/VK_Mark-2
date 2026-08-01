#include "pch.h"

#include "Device.h"

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData)
{
	(void)messageSeverity;
	(void)messageType;
	(void)pUserData;

	fmt::println("[Vulkan Validation Layer]: {}", pCallbackData->pMessage);
	return VK_FALSE;
}

void Device::IdleDevice() const { vkDeviceWaitIdle(m_context.device); }

void Device::Cleanup()
{
	m_graphicsQueue.CleanupFencePools();
	m_presentQueue.CleanupFencePools();
	m_transferQueue.CleanupFencePools();
	m_graphicsQueue.DestroyTimelineSemaphore();
	m_transferQueue.DestroyTimelineSemaphore();
	m_computeQueue.CleanupFencePools();
	m_computeQueue.DestroyTimelineSemaphore();

	m_threadCmdPoolManager.Cleanup(*this);

	vkDestroySurfaceKHR(m_context.instance, m_surface, nullptr);

	vkDestroyDevice(m_context.device, nullptr);

	if (Debugging.IsValidationLayerEnabled())
	{
		DestroyDebugUtilsMessengerEXT(m_context.instance, nullptr);
	}
	vkDestroyInstance(m_context.instance, nullptr);
}

void Device::CreateInstance()
{
	VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Mark-3";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 4, 0);
	appInfo.pEngineName = "Engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_4;

	VkInstanceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;

	auto reqExtensions = GetRequiredExtensions();
	createInfo.enabledExtensionCount = static_cast<uint32_t>(reqExtensions.size());
	createInfo.ppEnabledExtensionNames = reqExtensions.data();

	if (Debugging.IsValidationLayerEnabled())
	{
		if (CheckValidationLayerSupport())
		{
			createInfo.enabledLayerCount = static_cast<uint32_t>(m_validationLayers.size());
			createInfo.ppEnabledLayerNames = m_validationLayers.data();

			VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
			PopulateDebugMessengerCreateInfo(debugCreateInfo);

			if (Debugging.IsGpuAssistedValidationEnabled())
			{
				fmt::println("////GPU ASSISTED VALIDATION LAYERS ON////");

				static VkValidationFeatureEnableEXT gpuEnableFeatures[]
				{
					VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT,
					VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT
				};

				static VkValidationFeaturesEXT gpuValidationFeatures {
					.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
					.pNext = &debugCreateInfo,
					.enabledValidationFeatureCount = 2,
					.pEnabledValidationFeatures = gpuEnableFeatures
				};

				createInfo.pNext = &gpuValidationFeatures;
			}
			else
			{
				createInfo.pNext = &debugCreateInfo;
			}
		}
		else
		{
			createInfo.enabledLayerCount = 0;
			createInfo.pNext = nullptr;
			fmt::println("Validation layers requested, but not available!");
		}
	}

	VK_CHECK(vkCreateInstance(&createInfo, nullptr, &m_context.instance));

	SetupDebugMessenger();
}

// ------------------------
// Glfw specific functions
void Device::CreateSurface(GLFWwindow* windowHandle)
{
	VK_CHECK(glfwCreateWindowSurface(m_context.instance, windowHandle, nullptr, &m_surface));
}

std::vector<const char*> Device::GetRequiredExtensions() const
{
	uint32_t glfwExtensionsCount = 0;
	const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionsCount);

	ASSERT(glfwExtensions && glfwExtensionsCount > 0 && "GLFW failed to return instance extensions");

	std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionsCount);

	if (Debugging.IsValidationLayerEnabled())
	{
		extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
	}

	return extensions;
}
// ------------------------


void Device::InitLogical(const PhysicalDeviceCandidate& candidate)
{
	ASSERT(candidate.pDevice != VK_NULL_HANDLE);

	m_pDeviceName             = candidate.name;
	m_context.physicalDevice  = candidate.pDevice;
	m_deviceProps             = candidate.properties;
	m_deviceLimits            = candidate.limits;
	m_context.queueIndices    = candidate.queueIndices;
	m_swapchainSupportDetails = candidate.swapchainSupport;

	// -------------------------
	// Device queues assignment
	//--------------------------

	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
	std::set<uint32_t> uniqueQueueFamilies;

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(m_context.physicalDevice, &queueFamilyCount, nullptr);

	std::vector<VkQueueFamilyProperties> queueFamilyProps(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(
		m_context.physicalDevice,
		&queueFamilyCount,
		queueFamilyProps.data()
	);

	const auto qIndices = m_context.queueIndices;

	// Only add queue families that exist
	if (qIndices.graphicsFamily.has_value()) // Graphics
	{
		uint32_t gFamilyIndex   = qIndices.graphicsFamily.value();
		uint32_t gTimestampBits = queueFamilyProps[gFamilyIndex].timestampValidBits;
		uniqueQueueFamilies.insert(gFamilyIndex);
		m_graphicsQueue.Setup(gFamilyIndex, gTimestampBits, QueueType::Graphics);
	}
	if (qIndices.presentFamily.has_value()) // Present
	{
		uint32_t pFamilyIndex   = qIndices.presentFamily.value();
		uint32_t pTimestampBits = queueFamilyProps[pFamilyIndex].timestampValidBits;
		uniqueQueueFamilies.insert(pFamilyIndex);
		m_presentQueue.Setup(pFamilyIndex, pTimestampBits, QueueType::Present);
	}
	if (qIndices.transferFamily.has_value()) // Transfer
	{
		uint32_t tFamilyIndex   = qIndices.transferFamily.value();
		uint32_t tTimestampBits = queueFamilyProps[tFamilyIndex].timestampValidBits;
		uniqueQueueFamilies.insert(tFamilyIndex);
		m_transferQueue.Setup(tFamilyIndex, tTimestampBits, QueueType::Transfer);
	}
	if (qIndices.computeFamily.has_value()) // Compute
	{
		uint32_t cFamilyIndex   = qIndices.computeFamily.value();
		uint32_t cTimestampBits = queueFamilyProps[cFamilyIndex].timestampValidBits;
		uniqueQueueFamilies.insert(cFamilyIndex);
		m_computeQueue.Setup(cFamilyIndex, cTimestampBits, QueueType::Compute);
	}

	const float queuePriority = 1.0f;
	for (auto queueFamily : uniqueQueueFamilies)
	{
		queueCreateInfos.emplace_back(VkDeviceQueueCreateInfo{
			.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
			.queueFamilyIndex = queueFamily,
			.queueCount       = 1,
			.pQueuePriorities = &queuePriority
		});
	}

	// ----------------
	// Device features
	//-----------------

	VkPhysicalDeviceFeatures2 baseFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	baseFeatures.features.fillModeNonSolid                        = VK_TRUE;
	baseFeatures.features.samplerAnisotropy                       = VK_TRUE;
	baseFeatures.features.multiDrawIndirect                       = VK_TRUE;  // indirect draws enabled
	baseFeatures.features.shaderInt64                             = VK_TRUE;  // 64-bit addressing
	//baseFeatures.features.tessellationShader                      = VK_TRUE;
	baseFeatures.features.geometryShader                          = VK_TRUE;
	baseFeatures.features.depthBiasClamp                          = VK_TRUE;
	baseFeatures.features.depthClamp                              = VK_TRUE;
	baseFeatures.features.drawIndirectFirstInstance               = VK_TRUE;
	baseFeatures.features.imageCubeArray                          = VK_TRUE;
	baseFeatures.features.shaderStorageImageExtendedFormats       = VK_TRUE;
	baseFeatures.features.robustBufferAccess                      = VK_TRUE;
	baseFeatures.features.shaderInt16                             = VK_TRUE;
	baseFeatures.features.independentBlend                        = VK_TRUE;
	baseFeatures.features.fullDrawIndexUint32                     = VK_TRUE;

	VkPhysicalDeviceVulkan11Features features11{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES };
	features11.shaderDrawParameters                               = VK_TRUE;  // InstanceIndex
	features11.variablePointers                                   = VK_TRUE;
	features11.variablePointersStorageBuffer                      = VK_TRUE;
	//features11.multiview                                          = VK_TRUE;
	features11.storageBuffer16BitAccess                           = VK_TRUE;

	VkPhysicalDeviceVulkan12Features features12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	features12.bufferDeviceAddress                                = VK_TRUE;  // GPU pointers
	features12.descriptorIndexing                                 = VK_TRUE;  // Bindless descriptors
	features12.timelineSemaphore                                  = VK_TRUE;  // Timeline sync (async GPU workloads)
	features12.scalarBlockLayout                                  = VK_TRUE;  // No struct padding for gpu buffers
	features12.descriptorBindingUpdateUnusedWhilePending          = VK_TRUE;
	features12.descriptorBindingSampledImageUpdateAfterBind       = VK_TRUE;
	features12.descriptorBindingStorageImageUpdateAfterBind       = VK_TRUE;
	features12.descriptorBindingStorageBufferUpdateAfterBind      = VK_TRUE;
	features12.descriptorBindingUniformBufferUpdateAfterBind      = VK_TRUE;
	features12.descriptorBindingPartiallyBound                    = VK_TRUE;
	features12.descriptorBindingVariableDescriptorCount           = VK_TRUE;
	features12.runtimeDescriptorArray                             = VK_TRUE;
	features12.shaderSampledImageArrayNonUniformIndexing          = VK_TRUE;
	features12.shaderStorageImageArrayNonUniformIndexing          = VK_TRUE;
	features12.drawIndirectCount                                  = VK_TRUE;
	features12.shaderFloat16                                      = VK_TRUE;
	features12.shaderInt8                                         = VK_TRUE;
	features12.storageBuffer8BitAccess                            = VK_TRUE;
	features12.shaderBufferInt64Atomics                           = VK_TRUE;
	features12.hostQueryReset                                     = VK_TRUE;

	VkPhysicalDeviceVulkan13Features features13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
	features13.dynamicRendering                                   = VK_TRUE;
	features13.synchronization2                                   = VK_TRUE;
	features13.maintenance4                                       = VK_TRUE;
	features13.shaderDemoteToHelperInvocation                     = VK_TRUE;
	features13.subgroupSizeControl                                = VK_TRUE;
	features13.inlineUniformBlock                                 = VK_TRUE;
	features13.descriptorBindingInlineUniformBlockUpdateAfterBind = VK_TRUE;

	VkPhysicalDeviceVulkan14Features features14{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES };
	features14.pushDescriptor                                     = VK_TRUE;
	features14.maintenance5                                       = VK_TRUE;
	features14.maintenance6                                       = VK_TRUE;

	// =======================
	// Ray tracing extensions
	VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures
	{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR
	};
	accelerationStructureFeatures.accelerationStructure                                 = VK_TRUE;
	accelerationStructureFeatures.accelerationStructureCaptureReplay                    = VK_TRUE;
	accelerationStructureFeatures.descriptorBindingAccelerationStructureUpdateAfterBind = VK_TRUE;

	VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingPipelineFeatures
	{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR
	};
	rayTracingPipelineFeatures.rayTracingPipeline                                    = VK_TRUE;
	rayTracingPipelineFeatures.rayTracingPipelineShaderGroupHandleCaptureReplay      = VK_TRUE;
	rayTracingPipelineFeatures.rayTracingPipelineTraceRaysIndirect                   = VK_TRUE;
	rayTracingPipelineFeatures.rayTraversalPrimitiveCulling                          = VK_TRUE;

	VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures
	{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR
	};
	rayQueryFeatures.rayQuery = VK_TRUE;


	// =======================
	// Mesh shaders extension
	VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures
	{
		VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT
	};

	meshShaderFeatures.taskShader                             = VK_TRUE;
	meshShaderFeatures.meshShader                             = VK_TRUE;
	meshShaderFeatures.meshShaderQueries                      = VK_TRUE;
	//meshShaderFeatures.multiviewMeshShader                    = VK_TRUE;

	REQUIRE_HARDWARE(meshShaderFeatures.meshShader && meshShaderFeatures.taskShader, "20 series and up only");


	features14.pNext = &accelerationStructureFeatures;
	accelerationStructureFeatures.pNext = &rayTracingPipelineFeatures;
	rayTracingPipelineFeatures.pNext = &rayQueryFeatures;
	rayQueryFeatures.pNext = &meshShaderFeatures;
	meshShaderFeatures.pNext = nullptr;

	//features14.pNext   = nullptr;
	features13.pNext   = &features14;
	features12.pNext   = &features13;
	features11.pNext   = &features12;

	baseFeatures.pNext = &features11;

	VkDeviceCreateInfo createInfo{};
	createInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.pNext                   = &baseFeatures;
	createInfo.queueCreateInfoCount    = static_cast<uint32_t>(queueCreateInfos.size());
	createInfo.pQueueCreateInfos       = queueCreateInfos.data();
	createInfo.enabledExtensionCount   = static_cast<uint32_t>(m_deviceExtensions.size());
	createInfo.ppEnabledExtensionNames = m_deviceExtensions.data();
	createInfo.enabledLayerCount       = Debugging.IsValidationLayerEnabled() ? static_cast<uint32_t>(m_validationLayers.size()) : 0;
	createInfo.ppEnabledLayerNames     = Debugging.IsValidationLayerEnabled() ? m_validationLayers.data() : nullptr;

	VK_CHECK(vkCreateDevice(m_context.physicalDevice, &createInfo, nullptr, &m_context.device));

	LoadDeviceExtensionFunctions(m_context.device);

	// --------------------
	// Initialize VkQueues
	// --------------------
	// Retrieve queues only if they were created
	if (qIndices.graphicsFamily.has_value())
	{
		m_graphicsQueue.GetDeviceQueue(m_context);
		m_graphicsQueue.InitTimelineSemaphore();
	}

	if (qIndices.presentFamily.has_value())
	{
		m_presentQueue.GetDeviceQueue(m_context);
	}

	if (qIndices.transferFamily.has_value())
	{
		m_transferQueue.GetDeviceQueue(m_context);
		m_transferQueue.InitTimelineSemaphore();
	}

	if (qIndices.computeFamily.has_value())
	{
		m_computeQueue.GetDeviceQueue(m_context);
		m_computeQueue.InitTimelineSemaphore();
	}
}

void Device::InitThreadCommandPool(uint32_t threadCount)
{
	ASSERT(m_context.device != VK_NULL_HANDLE);
	// --------------------------------
	// Mulithreaded command pool setup
	// --------------------------------
	m_threadCmdPoolManager.Init(*this, threadCount);
}

VkCommandPool Device::CreateCommandPool(QueueType qType)
{
	uint32_t qFamilyIndex = UINT32_MAX;
	switch(qType)
	{
	case QueueType::Graphics:
		qFamilyIndex = m_graphicsQueue.GetFamilyIndex();
		break;
	case QueueType::Transfer:
		qFamilyIndex = m_transferQueue.GetFamilyIndex();
		break;
	case QueueType::Compute:
		qFamilyIndex = m_computeQueue.GetFamilyIndex();
		break;
	}
	ASSERT(qFamilyIndex != UINT32_MAX);

	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = qFamilyIndex;

	VkCommandPool commandPool;
	VK_CHECK(vkCreateCommandPool(m_context.device, &poolInfo, nullptr, &commandPool));

	return commandPool;
}

VkCommandBuffer Device::CreateCommandBuffer(VkCommandPool commandPool) const
{
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer;
	VK_CHECK(vkAllocateCommandBuffers(m_context.device, &allocInfo, &commandBuffer));

	return commandBuffer;
}

void Device::CreateCommandBuffers(VkCommandPool commandPool, VkCommandBuffer* commands, uint32_t count) const
{
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = count;

	VK_CHECK(vkAllocateCommandBuffers(m_context.device, &allocInfo, commands));
}

VkCommandBuffer Device::CreateSecondaryCommand(VkCommandPool pool, VkCommandBufferInheritanceInfo& inheritance) const
{
	VkCommandBufferAllocateInfo allocInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = pool,
		.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY,
		.commandBufferCount = 1
	};

	VkCommandBuffer secondaryCmd;
	vkAllocateCommandBuffers(m_context.device, &allocInfo, &secondaryCmd);

	VkCommandBufferBeginInfo beginInfo
	{
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
		.pInheritanceInfo = &inheritance
	};

	vkBeginCommandBuffer(secondaryCmd, &beginInfo);

	return secondaryCmd;
}

void Device::RecordCommand(
	std::function<void(VkCommandBuffer)>&& function,
	VkCommandBuffer commandBuffer,
	VkCommandBufferUsageFlags usageFlags)
{
	ASSERT(commandBuffer != VK_NULL_HANDLE && "[RecordCommandBuffer] Null command buffer.");

	VK_CHECK(vkResetCommandBuffer(commandBuffer, 0));

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = usageFlags;

	VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
	function(commandBuffer);
	VK_CHECK(vkEndCommandBuffer(commandBuffer));
}

void Device::RecordDeferredCommand(
	std::function<void(VkCommandBuffer)>&& function,
	VkCommandPool cmdPool,
	QueueType qType)
{
	VkCommandBuffer cmd = CreateCommandBuffer(cmdPool);
	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	VkCommandBufferBeginInfo cmdBeginInfo {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
	function(cmd);
	VK_CHECK(vkEndCommandBuffer(cmd));

	bool validQueueType = false;
	switch (qType) {
	case(QueueType::Graphics):
		PushGraphics(cmd);
		validQueueType = true;
		break;
	case(QueueType::Transfer):
		PushTransfer(cmd);
		validQueueType = true;
		break;
	case(QueueType::Compute):
		PushCompute(cmd);
		validQueueType = true;
		break;
	default:
		ASSERT(validQueueType);
	}
}

void Device::SubmitDeferredCommands(QueueType qType)
{
	bool validQueueType = false;
	std::vector<VkCommandBuffer> cmds;
	switch (qType)
	{
	case(QueueType::Graphics):
		cmds = DeferredCmds.CollectGraphics();
		m_graphicsQueue.SubmitCommand(cmds);
		validQueueType = true;
		break;
	case(QueueType::Transfer):
		cmds = DeferredCmds.CollectTransfer();
		m_transferQueue.SubmitCommand(cmds);
		validQueueType = true;
		break;
	case(QueueType::Compute):
		cmds = DeferredCmds.CollectCompute();
		m_computeQueue.SubmitCommand(cmds);
		validQueueType = true;
		break;
	default:
		ASSERT(validQueueType);
	}
}

// Thread command pools
void Device::ThreadCommandPoolManager::Init(Device& device, uint32_t threadCount)
{
	m_perThreadPools.resize(threadCount);
	for (uint32_t i = 0; i < threadCount; ++i)
	{
		auto& pool = m_perThreadPools[i];
		pool.graphicsPool = device.CreateCommandPool(QueueType::Graphics);
		pool.transferPool = device.CreateCommandPool(QueueType::Transfer);
		pool.computePool = device.CreateCommandPool(QueueType::Compute);
	}
}
void Device::ThreadCommandPoolManager::Cleanup(Device& device)
{
	for (uint32_t i = 0; i < m_perThreadPools.size(); ++i)
	{
		auto& pool = m_perThreadPools[i];

		if (pool.graphicsPool)
		{
			vkDestroyCommandPool(device.m_context.device, pool.graphicsPool, nullptr);
		}

		if (pool.transferPool)
		{
			vkDestroyCommandPool(device.m_context.device, pool.transferPool, nullptr);
		}

		if (pool.computePool)
		{
			vkDestroyCommandPool(device.m_context.device, pool.computePool, nullptr);
		}
	}
}

uint32_t Device::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
{
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(m_context.physicalDevice, &memProperties);

	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
	{
		if ((typeFilter & (1 << i)) &&
			(memProperties.memoryTypes[i].propertyFlags & properties) == properties)
		{
			return i;
		}
	}

	return 0;
}

std::vector<uint32_t> Device::FindSupportedSampleCounts() const
{
	std::vector<uint32_t> sampleCounts;

	VkSampleCountFlags counts = m_deviceLimits.framebufferColorSampleCounts & m_deviceLimits.framebufferDepthSampleCounts;

	if (counts & VK_SAMPLE_COUNT_8_BIT) { sampleCounts.push_back(8); }
	if (counts & VK_SAMPLE_COUNT_4_BIT) { sampleCounts.push_back(4); }
	if (counts & VK_SAMPLE_COUNT_2_BIT) { sampleCounts.push_back(2); }
	sampleCounts.push_back(1); // Always allow no MSAA

	return sampleCounts;
}


VkFormat Device::FindDepthFormat()
{
	return FindSupportedFormat(
		{ VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT },
		VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
	);
}

VkFormat Device::FindSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags feature)
{
	for (const auto& format : candidates)
	{
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(m_context.physicalDevice, format, &props);

		if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & feature) == feature)
		{
			return format;
		}
		else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & feature) == feature)
		{
			return format;
		}
	}

	return VK_FORMAT_UNDEFINED;
}

bool Device::HasStencilComponent(VkFormat format)
{
	return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

VkResult Device::CreateDebugUtilsMessengerEXT(
	const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
	const VkAllocationCallbacks* pAllocator)
{
	auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_context.instance, "vkCreateDebugUtilsMessengerEXT");
	if (func != nullptr)
	{
		return func(m_context.instance, pCreateInfo, pAllocator, &m_debugMessenger);
	}
	else
	{
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}
}

void Device::DestroyDebugUtilsMessengerEXT(
	VkInstance instance,
	const VkAllocationCallbacks* pAllocator) const
{
	auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
	if (func != nullptr)
		func(instance, m_debugMessenger, pAllocator);
}

void Device::PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo)
{
	createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	createInfo.messageSeverity =
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	createInfo.messageType =
		VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	createInfo.pfnUserCallback = DebugCallback;
}

void Device::SetupDebugMessenger()
{
	if (!Debugging.IsValidationLayerEnabled()) return;

	ASSERT(m_debugMessenger == VK_NULL_HANDLE && "debugMessenger should not be initialized yet");

	VkDebugUtilsMessengerCreateInfoEXT createInfo;
	PopulateDebugMessengerCreateInfo(createInfo);

	// Instance and debugMessenger are declared before this function is called
	CreateDebugUtilsMessengerEXT(&createInfo, nullptr);
}

bool Device::CheckValidationLayerSupport()
{
	uint32_t layerCount;
	vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

	std::vector<VkLayerProperties> availableLayers(layerCount);
	vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

	for (const char* layerName : m_validationLayers)
	{
		bool layerFound = false;

		for (const auto& layerProperties : availableLayers)
		{
			if (strcmp(layerName, layerProperties.layerName) == 0)
			{
				layerFound = true;
				break;
			}
		}

		if (!layerFound) return false;
	}

	return true;
}

SwapchainSupportDetails Device::GetSwapchainSupportDetails() const
{
	SwapchainSupportDetails details = m_swapchainSupportDetails;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
		m_context.physicalDevice,
		m_surface,
		&details.capabilities);
	return details;
}
