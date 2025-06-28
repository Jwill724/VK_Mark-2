#include "pch.h"

#include "CommandBuffer.h"
#include "core/EngineState.h"

VkCommandPool CommandBuffer::createCommandPool(VkDevice device, uint32_t queueFamilyIndex) {
	ASSERT(queueFamilyIndex != UINT32_MAX);

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

VkCommandBuffer CommandBuffer::createSecondaryCmd(VkDevice device, VkCommandPool pool, VkCommandBufferInheritanceInfo& inheritance) {
	VkCommandBufferAllocateInfo allocInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = pool,
		.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY,
		.commandBufferCount = 1
	};

	VkCommandBuffer secondaryCmd;
	vkAllocateCommandBuffers(device, &allocInfo, &secondaryCmd);

	VkCommandBufferBeginInfo beginInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
		.pInheritanceInfo = &inheritance
	};

	vkBeginCommandBuffer(secondaryCmd, &beginInfo);

	return secondaryCmd;
}

void CommandBuffer::recordDeferredCmd(std::function<void(VkCommandBuffer)>&& function, VkCommandPool cmdPool, QueueType type) {
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

	bool validQueueType = false;
	switch (type) {
	case(QueueType::Graphics):
		DeferredCmdSubmitQueue::pushGraphics(cmd);
		validQueueType = true;
		break;
	case(QueueType::Transfer):
		DeferredCmdSubmitQueue::pushTransfer(cmd);
		validQueueType = true;
		break;
	case(QueueType::Compute):
		DeferredCmdSubmitQueue::pushCompute(cmd);
		validQueueType = true;
		break;
	default:
		ASSERT(validQueueType && "[recordDeferredCmd] Invalid queue type.\n");
	}
}