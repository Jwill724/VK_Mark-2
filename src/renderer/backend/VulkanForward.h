#pragma once

#include <cstdint>

typedef struct VkDevice_T*                   VkDevice;
typedef struct VkPipeline_T*                 VkPipeline;
typedef struct VkDescriptorSet_T*            VkDescriptorSet;
typedef struct VkDescriptorSetLayout_T*      VkDescriptorSetLayout;
typedef struct VkSemaphore_T*                VkSemaphore;
typedef struct VkFence_T*                    VkFence;
typedef struct VkCommandBuffer_T*            VkCommandBuffer;
typedef struct VkCommandPool_T*              VkCommandPool;
typedef struct VkBuffer_T*                   VkBuffer;
typedef struct VkInstance_T*                 VkInstance;
typedef struct VkSurfaceKHR_T*               VkSurfaceKHR;
typedef struct VkPhysicalDevice_T*           VkPhysicalDevice;
typedef struct VkQueue_T*                    VkQueue;
typedef struct VkImage_T*                    VkImage;
typedef struct VkImageView_T*                VkImageView;
typedef struct VkShaderModule_T*             VkShaderModule;
typedef struct VkPipelineLayout_T*           VkPipelineLayout;
typedef struct VkSampler_T*                  VkSampler;
typedef struct VkSwapchainKHR_T*             VkSwapchainKHR;
typedef struct VkDebugUtilsMessengerEXT_T*   VkDebugUtilsMessengerEXT;
typedef struct VkQueryPool_T*                VkQueryPool;
typedef struct VkAccelerationStructureKHR_T* VkAccelerationStructureKHR;
typedef struct VkDeviceMemory_T*             VkDeviceMemory;

#if !defined(VK_NULL_HANDLE)
	#define VK_NULL_HANDLE 0
#endif

typedef uint32_t VkFlags;
typedef uint64_t VkDeviceSize;
