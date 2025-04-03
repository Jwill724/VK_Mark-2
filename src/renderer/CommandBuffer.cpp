#include "pch.h"

#include "CommandBuffer.h"

#include "utils/BufferUtils.h"
#include "utils/RendererUtils.h"
#include "vulkan/Backend.h"
#include "renderer.h"

// TODO: multi-threaded setup via #include <thread>

VkCommandPool CommandBuffer::createCommandPool(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flags) {
	VkDevice device = Backend::getDevice();

	if (queueFamilyIndex == UINT32_MAX) {
		throw std::runtime_error("Invalid queue family index for command pool creation!");
	}

	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = flags;
	poolInfo.queueFamilyIndex = queueFamilyIndex;

	VkCommandPool commandPool;
	VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool));

	return commandPool;
}

VkCommandBuffer CommandBuffer::createCommandBuffer(VkCommandPool commandPool) {
	VkDevice device = Backend::getDevice();

	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = commandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer;
	VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));

	return commandBuffer;
}

// Transfer and graphics queue usable
void CommandBuffer::setupImmediateCmdBuffer(ImmCmdSubmitDef& cmdSubmit, VkQueue queue) {

	uint32_t queueIndex;
	if (queue == Backend::getGraphicsQueue()) {
		queueIndex = Backend::getQueueFamilyIndices().graphicsFamily.value();
	}
	else if (queue == Backend::getTransferQueue()) {
		queueIndex = Backend::getQueueFamilyIndices().transferFamily.value();
	}
	else {
		throw std::runtime_error("Invalid queue passed into setupImmediateCmdBuffer");
	}

	cmdSubmit.immediateCmdPool = CommandBuffer::createCommandPool(queueIndex, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
	cmdSubmit.immediateCmdBuffer = CommandBuffer::createCommandBuffer(cmdSubmit.immediateCmdPool);
	cmdSubmit.immediateFence = RendererUtils::createFence();

	// only want graphics pools throughout program lifetime
	if (queueIndex == Backend::getQueueFamilyIndices().graphicsFamily.value()) {
		Engine::getDeletionQueue().push_function([=] {
			vkDestroyCommandPool(Backend::getDevice(), cmdSubmit.immediateCmdPool, nullptr);
			vkDestroyFence(Backend::getDevice(), cmdSubmit.immediateFence, nullptr);
		});
	}
	// will flush away the pool and fence when the rendering begins
	else {
		Renderer::getRenderImageDeletionQueue().push_function([=] {
			vkDestroyCommandPool(Backend::getDevice(), cmdSubmit.immediateCmdPool, nullptr);
			vkDestroyFence(Backend::getDevice(), cmdSubmit.immediateFence, nullptr);
		});
	}
}

void CommandBuffer::immediateCmdSubmit(std::function<void(VkCommandBuffer cmd)>&& function, ImmCmdSubmitDef& cmdSubmit, VkQueue queue) {
	VK_CHECK(vkResetFences(Backend::getDevice(), 1, &cmdSubmit.immediateFence));
	VK_CHECK(vkResetCommandBuffer(cmdSubmit.immediateCmdBuffer, 0));

	VkCommandBufferBeginInfo cmdBeginInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
	};

	VK_CHECK(vkBeginCommandBuffer(cmdSubmit.immediateCmdBuffer, &cmdBeginInfo));
	function(cmdSubmit.immediateCmdBuffer);
	VK_CHECK(vkEndCommandBuffer(cmdSubmit.immediateCmdBuffer));

	VkCommandBufferSubmitInfo immCmdInfo = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
		.commandBuffer = cmdSubmit.immediateCmdBuffer,
		.deviceMask = 0
	};

	VkSubmitInfo2 immSubmitInfo = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &immCmdInfo,
	};

	VK_CHECK(vkQueueSubmit2(queue, 1, &immSubmitInfo, cmdSubmit.immediateFence));

	VK_CHECK(vkWaitForFences(Backend::getDevice(), 1, &cmdSubmit.immediateFence, true, UINT64_MAX));
}