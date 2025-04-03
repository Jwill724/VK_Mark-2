#pragma once

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include <functional>
#include <deque>
#include <optional>
#include <span>
#include <string>

struct AllocatedImage {
	VkImage image;
	VkImageView imageView;
	VkFormat imageFormat;
	VmaAllocation allocation;
	VkExtent3D imageExtent;
	bool mipmapped;
};

// Device getters are required for this to work
struct DeletionQueue {
	std::deque<std::function<void()>> deletors;

	void push_function(std::function<void()>&& function) {
		deletors.push_back(function);
	}

	void flush() {
		// reverse iterate the deletion queue to execute all the functions
		for (auto it = deletors.rbegin(); it != deletors.rend(); ++it) {
			(*it)(); //call functors
		}

		deletors.clear();
	}
};

// Defines push constants usages
struct PushConstantDef {
	bool enabled;
	uint32_t offset;
	uint32_t size;
	VkShaderStageFlags stageFlags;
};

// Push Constant memory pool
struct PushConstantPool {
	uint32_t maxSize;
	uint32_t usedSize;
};

struct PushConstantBlock {
	glm::vec4 data1;
	glm::vec4 data2;
	glm::vec4 data3;
	glm::vec4 data4;
};

// Final rendering data for recording
struct PipelineEffect {
	const char* name;

	VkPipeline pipeline;
	VkPipelineLayout layout;

	// Push constant data
	PushConstantBlock data;
	PushConstantDef pcInfo;
};

struct DescriptorInfo {
	VkDescriptorType type;
	uint32_t binding = 0;
	VkShaderStageFlags stageFlags;

	void* pNext = nullptr;
};

struct ShaderStageInfo {
	const char* filePath;
	VkShaderStageFlagBits stage;
};

// Immediate command submit
struct ImmCmdSubmitDef {
	VkFence immediateFence = VK_NULL_HANDLE;
	VkCommandPool immediateCmdPool = VK_NULL_HANDLE;
	VkCommandBuffer immediateCmdBuffer = VK_NULL_HANDLE;
};

struct AllocatedBuffer {
	VkBuffer buffer;
	VmaAllocation allocation;
	VmaAllocationInfo info;
};

struct Vertex {
	glm::vec3 position;
	float uv_x;
	glm::vec3 normal;
	float uv_y;
	glm::vec4 color;
};

// holds the resources needed for a mesh
struct GPUMeshBuffers {

	AllocatedBuffer indexBuffer;
	AllocatedBuffer vertexBuffer;
	VkDeviceAddress vertexBufferAddress;
};

// push constants for our mesh object draws
struct GPUDrawPushConstants {
	glm::mat4 worldMatrix;
	VkDeviceAddress vertexBuffer;
};

struct GPUSceneData {
	glm::mat4 view;
	glm::mat4 proj;
	glm::mat4 viewproj;
	glm::vec4 ambientColor;
	glm::vec4 sunlightDirection; // w for sun power
	glm::vec4 sunlightColor;
};