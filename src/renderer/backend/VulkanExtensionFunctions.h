#pragma once
#include <vulkan/vulkan.h>

extern PFN_vkCmdDrawMeshTasksEXT              pfn_vkCmdDrawMeshTasksEXT;
extern PFN_vkCmdDrawMeshTasksIndirectEXT      pfn_vkCmdDrawMeshTasksIndirectEXT;
extern PFN_vkCmdDrawMeshTasksIndirectCountEXT pfn_vkCmdDrawMeshTasksIndirectCountEXT;

//extern PFN_vkCreateAccelerationStructureKHR              pfn_vkCreateAccelerationStructureKHR;
//extern PFN_vkDestroyAccelerationStructureKHR             pfn_vkDestroyAccelerationStructureKHR;
//extern PFN_vkGetAccelerationStructureBuildSizesKHR       pfn_vkGetAccelerationStructureBuildSizesKHR;
//extern PFN_vkGetAccelerationStructureDeviceAddressKHR    pfn_vkGetAccelerationStructureDeviceAddressKHR;
//extern PFN_vkCmdBuildAccelerationStructuresKHR           pfn_vkCmdBuildAccelerationStructuresKHR;
//extern PFN_vkCmdCopyAccelerationStructureKHR             pfn_vkCmdCopyAccelerationStructureKHR;
//extern PFN_vkCmdWriteAccelerationStructuresPropertiesKHR pfn_vkCmdWriteAccelerationStructuresPropertiesKHR;
//
//extern PFN_vkCreateRayTracingPipelinesKHR       pfn_vkCreateRayTracingPipelinesKHR;
//extern PFN_vkGetRayTracingShaderGroupHandlesKHR pfn_vkGetRayTracingShaderGroupHandlesKHR;
//extern PFN_vkCmdTraceRaysKHR                    pfn_vkCmdTraceRaysKHR;
//extern PFN_vkCmdTraceRaysIndirectKHR            pfn_vkCmdTraceRaysIndirectKHR;

void LoadDeviceExtensionFunctions(VkDevice device);

#define vkCmdDrawMeshTasksEXT              pfn_vkCmdDrawMeshTasksEXT
#define vkCmdDrawMeshTasksIndirectEXT      pfn_vkCmdDrawMeshTasksIndirectEXT
#define vkCmdDrawMeshTasksIndirectCountEXT pfn_vkCmdDrawMeshTasksIndirectCountEXT

//#define vkCreateAccelerationStructureKHR              pfn_vkCreateAccelerationStructureKHR
//#define vkDestroyAccelerationStructureKHR             pfn_vkDestroyAccelerationStructureKHR
//#define vkGetAccelerationStructureBuildSizesKHR        pfn_vkGetAccelerationStructureBuildSizesKHR
//#define vkGetAccelerationStructureDeviceAddressKHR     pfn_vkGetAccelerationStructureDeviceAddressKHR
//#define vkCmdBuildAccelerationStructuresKHR            pfn_vkCmdBuildAccelerationStructuresKHR
//#define vkCmdCopyAccelerationStructureKHR              pfn_vkCmdCopyAccelerationStructureKHR
//#define vkCmdWriteAccelerationStructuresPropertiesKHR  pfn_vkCmdWriteAccelerationStructuresPropertiesKHR
//
//#define vkCreateRayTracingPipelinesKHR       pfn_vkCreateRayTracingPipelinesKHR
//#define vkGetRayTracingShaderGroupHandlesKHR pfn_vkGetRayTracingShaderGroupHandlesKHR
//#define vkCmdTraceRaysKHR                    pfn_vkCmdTraceRaysKHR
//#define vkCmdTraceRaysIndirectKHR            pfn_vkCmdTraceRaysIndirectKHR
