#include "VulkanExtensionFunctions.h"

#undef vkCmdDrawMeshTasksEXT 
#undef vkCmdDrawMeshTasksIndirectEXT
#undef vkCmdDrawMeshTasksIndirectCountEXT
//#undef vkCreateAccelerationStructureKHR
//#undef vkDestroyAccelerationStructureKHR
//#undef vkGetAccelerationStructureBuildSizesKHR
//#undef vkGetAccelerationStructureDeviceAddressKHR
//#undef vkCmdBuildAccelerationStructuresKHR
//#undef vkCmdCopyAccelerationStructureKHR
//#undef vkCmdWriteAccelerationStructuresPropertiesKHR
//#undef vkCreateRayTracingPipelinesKHR
//#undef vkGetRayTracingShaderGroupHandlesKHR
//#undef vkCmdTraceRaysKHR
//#undef vkCmdTraceRaysIndirectKHR

PFN_vkCmdDrawMeshTasksEXT              pfn_vkCmdDrawMeshTasksEXT              = nullptr;
PFN_vkCmdDrawMeshTasksIndirectEXT      pfn_vkCmdDrawMeshTasksIndirectEXT      = nullptr;
PFN_vkCmdDrawMeshTasksIndirectCountEXT pfn_vkCmdDrawMeshTasksIndirectCountEXT = nullptr;

//PFN_vkCreateAccelerationStructureKHR              pfn_vkCreateAccelerationStructureKHR              = nullptr;
//PFN_vkDestroyAccelerationStructureKHR             pfn_vkDestroyAccelerationStructureKHR             = nullptr;
//PFN_vkGetAccelerationStructureBuildSizesKHR       pfn_vkGetAccelerationStructureBuildSizesKHR       = nullptr;
//PFN_vkGetAccelerationStructureDeviceAddressKHR    pfn_vkGetAccelerationStructureDeviceAddressKHR    = nullptr;
//PFN_vkCmdBuildAccelerationStructuresKHR           pfn_vkCmdBuildAccelerationStructuresKHR           = nullptr;
//PFN_vkCmdCopyAccelerationStructureKHR             pfn_vkCmdCopyAccelerationStructureKHR             = nullptr;
//PFN_vkCmdWriteAccelerationStructuresPropertiesKHR pfn_vkCmdWriteAccelerationStructuresPropertiesKHR = nullptr;
//
//PFN_vkCreateRayTracingPipelinesKHR       pfn_vkCreateRayTracingPipelinesKHR       = nullptr;
//PFN_vkGetRayTracingShaderGroupHandlesKHR pfn_vkGetRayTracingShaderGroupHandlesKHR = nullptr;
//PFN_vkCmdTraceRaysKHR                    pfn_vkCmdTraceRaysKHR                    = nullptr;
//PFN_vkCmdTraceRaysIndirectKHR            pfn_vkCmdTraceRaysIndirectKHR            = nullptr;

#define LOAD(fn) pfn_##fn = reinterpret_cast<PFN_##fn>(vkGetDeviceProcAddr(device, #fn))

void LoadDeviceExtensionFunctions(VkDevice device)
{
	LOAD(vkCmdDrawMeshTasksEXT);
	LOAD(vkCmdDrawMeshTasksIndirectEXT);
	LOAD(vkCmdDrawMeshTasksIndirectCountEXT);

	//LOAD(vkCreateAccelerationStructureKHR);
	//LOAD(vkDestroyAccelerationStructureKHR);
	//LOAD(vkGetAccelerationStructureBuildSizesKHR);
	//LOAD(vkGetAccelerationStructureDeviceAddressKHR);
	//LOAD(vkCmdBuildAccelerationStructuresKHR);
	//LOAD(vkCmdCopyAccelerationStructureKHR);
	//LOAD(vkCmdWriteAccelerationStructuresPropertiesKHR);

	//LOAD(vkCreateRayTracingPipelinesKHR);
	//LOAD(vkGetRayTracingShaderGroupHandlesKHR);
	//LOAD(vkCmdTraceRaysKHR);
	//LOAD(vkCmdTraceRaysIndirectKHR);
}
#undef LOAD
