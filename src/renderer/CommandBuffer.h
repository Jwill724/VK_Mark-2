#pragma once

#include "common/Vk_Types.h"

namespace CommandBuffer {
	VkCommandPool createCommandPool(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags);
	VkCommandBuffer createCommandBuffer(VkCommandPool commandPool);
	void setupImmediateCmdBuffer(ImmCmdSubmitDef& immCmd);
	void immediateCmdSubmit(std::function<void(VkCommandBuffer cmd)>&& function, ImmCmdSubmitDef& cmdSubmit, VkQueue queue);
};