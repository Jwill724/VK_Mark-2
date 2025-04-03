#pragma once
#define GLM_FORCE_DEPTH_ZERO_TO_ONE

#include "utils/BufferUtils.h"
#include "vulkan/Backend.h"

//static bool hasStencilComponent(VkFormat format);
//
//struct Texture {
//	AllocatedImage createTexImage(void* data, VkExtent3D size, VkFormat format, VkImageUsageFlags usage, bool mipmapped);
//};


//struct Texture {
//
//	VkDevice _device = VK_NULL_HANDLE;
//	VkImage _textureImage = VK_NULL_HANDLE;
//	VkDeviceMemory _textureImageMemory = VK_NULL_HANDLE;
//	VkImageView _textureImageView = VK_NULL_HANDLE;
//	VkSampler _textureSampler = VK_NULL_HANDLE;
//
//	VkBuffer _textureStagingBuffer = VK_NULL_HANDLE;
//	VkDeviceMemory _textureStagingBufferMemory = VK_NULL_HANDLE;
//
//	int texWidth, texHeight, texChannels;
//
//	uint32_t _mipLevels = 0;
//	VkFormat _format;
//
//	void createTextureImage(const std::string& file);
//	void createImage(uint32_t width, uint32_t height, VkFormat format);
//
//	void generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels, VkCommandPool commandPool, VkQueue queue);
//	void createTextureSampler();
//	void transitionTextureImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels,
//		VkCommandPool commandPool, VkQueue queue);
//	void cleanup();
//};