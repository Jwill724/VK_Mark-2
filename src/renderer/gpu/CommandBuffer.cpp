#include "pch.h"

#include "CommandBuffer.h"
#include "core/EngineState.h"

VkCommandPool CommandBuffer::createCommandPool(VkDevice device, uint32_t queueFamilyIndex) {
	assert(queueFamilyIndex != UINT32_MAX);

	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = queueFamilyIndex;

	VkCommandPool commandPool;
	VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool));

	return commandPool;
}

VkCommandBuffer CommandBuffer::createCommandBuffer(VkDevice device, VkCommandPool commandPool) {
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer;
	VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));

	return commandBuffer;
}

void CommandBuffer::recordDeferredCmd(std::function<void(VkCommandBuffer)>&& function, VkCommandPool cmdPool, bool transferUse) {
	VkCommandBuffer cmd = createCommandBuffer(Backend::getDevice(), cmdPool);
	fmt::print("Allocated cmd: 0x{:X} from pool: 0x{:X}\n", (uint64_t)cmd, (uint64_t)cmdPool);
	VK_CHECK(vkResetCommandBuffer(cmd, 0));

	VkCommandBufferBeginInfo cmdBeginInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};

	VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));
	function(cmd);
	VK_CHECK(vkEndCommandBuffer(cmd));

	if (!transferUse) {
		DeferredCmdSubmitQueue::pushGraphics(cmd);
	}
	else {
		DeferredCmdSubmitQueue::pushTransfer(cmd);
	}
}