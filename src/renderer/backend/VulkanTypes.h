#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <array>
#include <optional>
#include <fmt/base.h>

inline const char* vkResultToString(VkResult result)
{
	switch (result)
	{
		case VK_SUCCESS:                       return "VK_SUCCESS";
		case VK_NOT_READY:                     return "VK_NOT_READY";
		case VK_TIMEOUT:                       return "VK_TIMEOUT";
		case VK_EVENT_SET:                     return "VK_EVENT_SET";
		case VK_EVENT_RESET:                   return "VK_EVENT_RESET";
		case VK_INCOMPLETE:                    return "VK_INCOMPLETE";
		case VK_ERROR_OUT_OF_HOST_MEMORY:      return "VK_ERROR_OUT_OF_HOST_MEMORY";
		case VK_ERROR_OUT_OF_DEVICE_MEMORY:    return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
		case VK_ERROR_INITIALIZATION_FAILED:   return "VK_ERROR_INITIALIZATION_FAILED";
		case VK_ERROR_DEVICE_LOST:             return "VK_ERROR_DEVICE_LOST";
		case VK_ERROR_MEMORY_MAP_FAILED:       return "VK_ERROR_MEMORY_MAP_FAILED";
		case VK_ERROR_LAYER_NOT_PRESENT:       return "VK_ERROR_LAYER_NOT_PRESENT";
		case VK_ERROR_EXTENSION_NOT_PRESENT:   return "VK_ERROR_EXTENSION_NOT_PRESENT";
		case VK_ERROR_FEATURE_NOT_PRESENT:     return "VK_ERROR_FEATURE_NOT_PRESENT";
		case VK_ERROR_INCOMPATIBLE_DRIVER:     return "VK_ERROR_INCOMPATIBLE_DRIVER";
		case VK_ERROR_TOO_MANY_OBJECTS:        return "VK_ERROR_TOO_MANY_OBJECTS";
		case VK_ERROR_FORMAT_NOT_SUPPORTED:    return "VK_ERROR_FORMAT_NOT_SUPPORTED";
		case VK_ERROR_FRAGMENTED_POOL:         return "VK_ERROR_FRAGMENTED_POOL";
		case VK_ERROR_UNKNOWN:                 return "VK_ERROR_UNKNOWN";
		default:                               return "UNKNOWN_ERROR";
	}
}

// Vulkan error checker
#define VK_CHECK(x)                                                \
	do {                                                           \
		VkResult err = x;                                          \
		if (err != VK_SUCCESS) {                                   \
			fmt::println(stderr,                                   \
				"[VK_CHECK] Vulkan Error: {} in file {} at line {}", \
				vkResultToString(err), __FILE__, __LINE__);        \
			abort();                                               \
		}                                                          \
	} while (0)


// Defines push constants usages
struct PushConstantDef
{
	uint32_t offset;
	uint32_t size;
	VkFlags stageFlags;
};

struct DescriptorInfo
{
	VkDescriptorType type;
	uint32_t binding = UINT32_MAX;
	VkFlags stageFlags;

	void* pNext = nullptr;
};

enum class QueueType
{
	Graphics,
	Transfer,
	Compute,
	Present,
	Nothing
};

// ---------------
// Pipeline types
// ---------------

enum class Vulkan_ShaderStage
{
	COMPUTE_STAGE  = VK_SHADER_STAGE_COMPUTE_BIT,
	VERTEX_STAGE   = VK_SHADER_STAGE_VERTEX_BIT,
	FRAGMENT_STAGE = VK_SHADER_STAGE_FRAGMENT_BIT,
	IMAGE_STAGES   = COMPUTE_STAGE | FRAGMENT_STAGE,
	ALL_STAGES     = COMPUTE_STAGE | VERTEX_STAGE | FRAGMENT_STAGE
};


// Holds pipeline layout and push constant data
// All pipelines use the same setup so its globally accessible
struct PipelineLayoutConst
{
	VkPipelineLayout pipelineLayout;
	PushConstantDef pushConstantDef;
};

struct PipelineHandle
{
	VkPipeline pipeline = VK_NULL_HANDLE;
	bool swappable = false;
	VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_MAX_ENUM;
	VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
	PipelineLayoutConst layout;
};

struct PipelinePreset
{
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

	// Default pipeline settings
	VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
	VkCullModeFlagBits cullMode = VK_CULL_MODE_NONE;
	VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	bool enableBlending = false;
	bool enableDepthTest = true;
	bool enableDepthWrite = false;
	VkCompareOp depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;

	bool enableDepthBias = false;
	float depthBiasConstant = 0.0f;
	float depthBiasSlope = 0.0f;

	std::vector<Vulkan_Format> colorFormats;

	Vulkan_Format depthFormat = Vulkan_Format::Undefined;

	std::vector<VkPipelineColorBlendAttachmentState> blendAttachments;

	bool IsDepthDefined() const noexcept { return depthFormat != Vulkan_Format::Undefined; }
	bool IsColorDefined() const noexcept { return !colorFormats.empty(); }
};

struct AttachmentDesc
{
	VkImageView imageView = VK_NULL_HANDLE;
	VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	VkClearValue clearValue{ 0.0f };

	AttachmentDesc() = default;
	AttachmentDesc(
		VkImageView view,
		VkImageLayout layout) {}

	void SetDepth(uint32_t value) { clearValue.depthStencil.depth = value; }
	void SetColor(VkClearColorValue value) { clearValue.color = value; }

	VkResolveModeFlagBits resolveMode = VK_RESOLVE_MODE_NONE;
	VkImageView resolveView = VK_NULL_HANDLE;
	VkImageLayout resolveLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

// --------------
// Image formats
// --------------

enum class Vulkan_Format
{
	RGBA16F     = VK_FORMAT_R16G16B16A16_SFLOAT,
	D32         = VK_FORMAT_D32_SFLOAT,
	BGRpacked   = VK_FORMAT_B10G11R11_UFLOAT_PACK32,
	R8unorm     = VK_FORMAT_R8_UNORM,
	RG8unorm    = VK_FORMAT_R8G8_UNORM,
	RGBA8unorm  = VK_FORMAT_R8G8B8A8_UNORM,
	ABGRpacked  = VK_FORMAT_A2B10G10R10_UNORM_PACK32,
	R16F        = VK_FORMAT_R16_SFLOAT,
	RG16F       = VK_FORMAT_R16G16_SFLOAT,
	R32U        = VK_FORMAT_R32_UINT,
	R32F        = VK_FORMAT_R32_SFLOAT,
	R8U         = VK_FORMAT_R8_UINT,
	Undefined   = VK_FORMAT_UNDEFINED
};

// ------------
// Descriptors
// ------------

struct DescriptorDef
{
	VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
};

enum class Vulkan_DescriptorType
{
	COPY_STAGING     = VK_DESCRIPTOR_TYPE_MAX_ENUM, // Doesn't need descriptor
	SSBO             = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
	UNIFORM          = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
	INLINE           = VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK,
	COMBINED_SAMPLER = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	STORAGE          = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
};

// -------------
// Buffer enums
// -------------

enum class BufferUsage
{
	DEFAULT,
	ADDRESS_TABLE = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
					VK_BUFFER_USAGE_TRANSFER_DST_BIT   |
					VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,

	VERTEX_PULL   = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
					VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
};

// -----
// Sync
// -----
struct TimelineSync
{
	VkSemaphore semaphore = VK_NULL_HANDLE;
	uint64_t signalValue = UINT64_MAX;
};

// --------
// Devices
// --------

struct QueueFamilyIndices
{
	std::optional<uint32_t> graphicsFamily;
	std::optional<uint32_t> presentFamily;
	std::optional<uint32_t> transferFamily;
	std::optional<uint32_t> computeFamily;

	bool IsComplete() const noexcept
	{
		return graphicsFamily.has_value() && presentFamily.has_value() && transferFamily.has_value() && computeFamily.has_value();
	}
};

struct SwapchainSupportDetails
{
	VkSurfaceCapabilitiesKHR capabilities{};
	std::vector<VkSurfaceFormatKHR> formats{};
	std::vector<VkPresentModeKHR> presentModes{};
};

struct DeviceContext
{
	VkDevice device = VK_NULL_HANDLE;
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkInstance instance = VK_NULL_HANDLE;
	QueueFamilyIndices queueIndices;
};

struct PhysicalDeviceCandidate
{
	std::string name;
	VkPhysicalDevice pDevice = VK_NULL_HANDLE;

	VkPhysicalDeviceProperties properties;
	VkPhysicalDeviceLimits     limits;

	QueueFamilyIndices queueIndices;
	SwapchainSupportDetails swapchainSupport;
};

// Commands
struct ThreadCommandPool
{
	VkCommandPool graphicsPool = VK_NULL_HANDLE;
	VkCommandPool transferPool = VK_NULL_HANDLE;

	ThreadCommandPool() = default;

	// Disallow copy
	ThreadCommandPool(const ThreadCommandPool&) = delete;
	ThreadCommandPool& operator=(const ThreadCommandPool&) = delete;

	// Allow move
	ThreadCommandPool(ThreadCommandPool&&) = default;
	ThreadCommandPool& operator=(ThreadCommandPool&&) = default;
};
