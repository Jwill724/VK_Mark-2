#include <pch.h>

#include "Queue.h"
#include "Sync.h"
#include "../frame/FrameResources.h"

void GPUQueue::WaitIdle() const
{
	VK_CHECK(vkQueueWaitIdle(m_queue));
}

void GPUQueue::GetDeviceQueue(const DeviceContext& deviceCtx)
{
	vkGetDeviceQueue(deviceCtx.device, m_familyIndex, 0, &m_queue);
	m_logicalDeviceCopy = deviceCtx.device;
}

void GPUQueue::CleanupFencePools()
{
	m_fencePool.DestroyFences(m_logicalDeviceCopy);
}

TimestampReadback GPUQueue::ReadTimestamps(
	VkQueryPool                             pool,
	std::span<const RD::PassTimestampRange> ranges,
	std::span<const bool>                   passUsed,
	float                                   timestampPeriod,
	bool                                    bReadFrameQueries)
{
	ASSERT(pool != VK_NULL_HANDLE);
	ASSERT(m_qType == QueueType::Compute || m_qType == QueueType::Graphics);

	TimestampReadback result{};
	for (size_t i = 0; i < ranges.size(); ++i)
	{
		if (!passUsed[i]) continue;

		uint64_t queryPair[2]{};

		VkResult res = vkGetQueryPoolResults(
			m_logicalDeviceCopy,
			pool,
			ranges[i].beginQuery,
			2,
			sizeof(queryPair),
			queryPair,
			sizeof(uint64_t),
			VK_QUERY_RESULT_64_BIT);

		if (res == VK_NOT_READY)
		{
			result.allReady = false;
			continue;
		}

		VK_CHECK(res);

		if (queryPair[1] > queryPair[0])
		{
			result.passResults[i].gpuMs = static_cast<float>(
				double(queryPair[1] - queryPair[0]) *
				double(timestampPeriod) / 1'000'000.0);

			result.passResults[i].valid = true;
		}
	}

	if (bReadFrameQueries)
	{
		uint64_t queryPair[2]{};

		VkResult res = vkGetQueryPoolResults(
			m_logicalDeviceCopy,
			pool,
			FRAME_BEGIN_QUERY,
			2,
			sizeof(queryPair),
			queryPair,
			sizeof(uint64_t),
			VK_QUERY_RESULT_64_BIT);

		if (res == VK_NOT_READY)
		{
			result.allReady = false;
		}
		else
		{
			VK_CHECK(res);

			if (queryPair[1] > queryPair[0])
			{
				result.frameResult.gpuMs = static_cast<float>(
					double(queryPair[1] - queryPair[0]) *
					double(timestampPeriod) / 1'000'000.0);

				result.frameResult.valid = true;
			}
		}
	}

	return result;
}


// -----------------------------
// Timeline semaphore functions
// -----------------------------

uint64_t GPUQueue::SubmitWithTimelineSync(
	const std::vector<VkCommandBuffer>& cmdBuffers,
	VkSemaphore timelineSemaphore,
	uint64_t signalValue,
	VkSemaphore waitSemaphore,
	uint64_t waitValue,
	VkPipelineStageFlags2 waitStages)
{
	std::lock_guard lock(m_mutex);

	// Ensure to signal a strictly greater value than current.
	uint64_t current = 0;
	VK_CHECK(vkGetSemaphoreCounterValue(m_logicalDeviceCopy, timelineSemaphore, &current));
	if (signalValue <= current) signalValue = current + 1;

	std::vector<VkSemaphoreSubmitInfo> waitInfos;
	if (waitSemaphore != VK_NULL_HANDLE)
	{
		waitInfos.emplace_back(VkSemaphoreSubmitInfo{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = waitSemaphore,
			.value = waitValue,
			.stageMask = waitStages,
			.deviceIndex = 0,
		});
	}

	std::vector<VkCommandBufferSubmitInfo> cmdInfos;
	cmdInfos.reserve(cmdBuffers.size());
	for (const auto cmd : cmdBuffers)
	{
		cmdInfos.emplace_back(VkCommandBufferSubmitInfo{
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
			.commandBuffer = cmd,
			.deviceMask = 1
		});
	}

	VkSemaphoreSubmitInfo signalInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
	signalInfo.semaphore   = timelineSemaphore;
	signalInfo.value       = signalValue;
	signalInfo.stageMask   = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	signalInfo.deviceIndex = 0;

	VkSubmitInfo2 submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
	submitInfo.waitSemaphoreInfoCount   = static_cast<uint32_t>(waitInfos.size());
	submitInfo.pWaitSemaphoreInfos      = waitInfos.data();
	submitInfo.commandBufferInfoCount   = static_cast<uint32_t>(cmdInfos.size());
	submitInfo.pCommandBufferInfos      = cmdInfos.data();
	submitInfo.signalSemaphoreInfoCount = 1;
	submitInfo.pSignalSemaphoreInfos    = &signalInfo;

	VK_CHECK(vkQueueSubmit2(m_queue, 1, &submitInfo, VK_NULL_HANDLE));

	return signalValue;
}

void GPUQueue::WaitTimelineValue(VkSemaphore semaphore, uint64_t waitValue)
{
	std::lock_guard lock(m_mutex);

	VkSemaphoreWaitInfo waitInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
	waitInfo.flags = 0;
	waitInfo.semaphoreCount = 1;
	waitInfo.pSemaphores = &semaphore;
	waitInfo.pValues = &waitValue;

	VK_CHECK(vkWaitSemaphores(m_logicalDeviceCopy, &waitInfo, UINT64_MAX));
}

void GPUQueue::Submit2(
	std::span<const VkSemaphoreSubmitInfo> waits,
	VkCommandBuffer cmd,
	std::span<const VkSemaphoreSubmitInfo> signals,
	VkFence fence)
{
	VkCommandBufferSubmitInfo cmdInfo{};
	cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	cmdInfo.commandBuffer = cmd;

	VkSubmitInfo2 submit{};
	submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	submit.waitSemaphoreInfoCount   = static_cast<uint32_t>(waits.size());
	submit.pWaitSemaphoreInfos      = waits.data();
	submit.commandBufferInfoCount   = 1u;
	submit.pCommandBufferInfos      = &cmdInfo;
	submit.signalSemaphoreInfoCount = static_cast<uint32_t>(signals.size());
	submit.pSignalSemaphoreInfos    = signals.data();

	VK_CHECK(vkQueueSubmit2(GetQueue(), 1u, &submit, fence));
}

// -----------------
// Fence management
// -----------------

VkFence GPUQueue::SubmitInfo(const VkSubmitInfo& info)
{
	VkFence fence = m_fencePool.GetFence(m_logicalDeviceCopy);
	VK_CHECK(vkQueueSubmit(m_queue, 1, &info, fence));
	return fence;
}

VkFence GPUQueue::SubmitInfo(const std::vector<VkSubmitInfo>& infos)
{
	VkFence fence = m_fencePool.GetFence(m_logicalDeviceCopy);
	VK_CHECK(vkQueueSubmit(m_queue, static_cast<uint32_t>(infos.size()), infos.data(), fence));
	return fence;
}

void GPUQueue::WaitAndRecycleLastFence(VkFence& lastSubmittedFence)
{
	ASSERT(lastSubmittedFence != VK_NULL_HANDLE);

	if (!m_fencePool.IsFenceReady(lastSubmittedFence, m_logicalDeviceCopy))
	{
		VK_CHECK(vkWaitForFences(m_logicalDeviceCopy, 1, &lastSubmittedFence, VK_TRUE, UINT64_MAX));
	}

	m_fencePool.Recycle(lastSubmittedFence);
	lastSubmittedFence = VK_NULL_HANDLE;
}


// -------------------
// Command submitting
// -------------------

void GPUQueue::SubmitCommand(VkCommandBuffer command)
{
	std::lock_guard lock(m_mutex);

	ASSERT(command != VK_NULL_HANDLE && "No command defined.");

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &command;

	VkFence fence = SubmitInfo(submitInfo);
	WaitAndRecycleLastFence(fence);
}

void GPUQueue::SubmitCommand(std::vector<VkCommandBuffer> commands)
{
	std::lock_guard lock(m_mutex);

	ASSERT(!commands.empty() && "No commands defined.");

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = static_cast<uint32_t>(commands.size());
	submitInfo.pCommandBuffers = commands.data();

	VkFence fence = SubmitInfo(submitInfo);
	WaitAndRecycleLastFence(fence);
}

// -------------------------
// Static creation function
// -------------------------

VkSemaphore GPUQueue::CreateNewSemaphore(VkDevice device)
{
	VkSemaphoreCreateInfo semaphoreInfo{};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkSemaphore semaphore;
	VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphore));

	return semaphore;
}

TimelineSync GPUQueue::CreateTimelineSemaphore(VkDevice device)
{
	VkSemaphoreTypeCreateInfo timelineCreateInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
		.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
		.initialValue = 0
	};

	VkSemaphoreCreateInfo createInfo{
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
		.pNext = &timelineCreateInfo
	};

	TimelineSync newtlSync;

	VK_CHECK(vkCreateSemaphore(device, &createInfo, nullptr, &newtlSync.semaphore));
	newtlSync.signalValue = 1;

	return newtlSync;
}

VkFence GPUQueue::CreateNewFence(VkDevice device)
{
	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	VkFence fence;
	VK_CHECK(vkCreateFence(device, &fenceInfo, nullptr, &fence));

	return fence;
}

void GPUQueue::DestroyFence(VkDevice device, VkFence fence)
{
	if (fence != VK_NULL_HANDLE)
		vkDestroyFence(device, fence, nullptr);
}
void GPUQueue::DestroySemaphore(VkDevice device, VkSemaphore semaphore)
{
	if (semaphore != VK_NULL_HANDLE)
		vkDestroySemaphore(device, semaphore, nullptr);
}


// --------------------------------------------------
// SPECIAL RENDERER QUEUES FOR FINAL GRAPHICS SUBMIT
// --------------------------------------------------

// Graphics queue
VkResult GraphicsQueue::SubmitFrame(
	const std::vector<VkSemaphoreSubmitInfo>& waitInfos,
	VkCommandBuffer                           cmdBuffer,
	VkSemaphore                               signalSemaphore,
	VkFence                                   fence)
{
	std::lock_guard lock(m_mutex);

	VkCommandBufferSubmitInfo cmdInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
	cmdInfo.commandBuffer = cmdBuffer;
	cmdInfo.deviceMask    = 1;

	VkSemaphoreSubmitInfo signalInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
	signalInfo.semaphore = signalSemaphore;
	signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

	VkSubmitInfo2 submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
	submitInfo.waitSemaphoreInfoCount   = static_cast<uint32_t>(waitInfos.size());
	submitInfo.pWaitSemaphoreInfos      = waitInfos.data();
	submitInfo.commandBufferInfoCount   = 1;
	submitInfo.pCommandBufferInfos      = &cmdInfo;
	submitInfo.signalSemaphoreInfoCount = 1;
	submitInfo.pSignalSemaphoreInfos    = &signalInfo;

	return vkQueueSubmit2(m_queue, 1, &submitInfo, fence);
}

void GraphicsQueue::InitTimelineSemaphore()
{
	ASSERT(m_logicalDeviceCopy != VK_NULL_HANDLE);
	m_sync = CreateTimelineSemaphore(m_logicalDeviceCopy);
}

void GraphicsQueue::DestroyTimelineSemaphore()
{
	DestroySemaphore(m_logicalDeviceCopy, m_sync.semaphore);
	m_sync = {};
}


// --------------------------------
// TIMELINE SYNC MANAGEMENT QUEUES
// --------------------------------

// Present queue
VkResult PresentQueue::Present(
	VkSwapchainKHR swapchain,
	uint32_t       imageIndex,
	VkSemaphore    waitSemaphore)
{
	std::lock_guard lock(m_mutex);

	VkPresentInfoKHR info{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
	info.swapchainCount     = 1;
	info.pSwapchains        = &swapchain;
	info.pImageIndices      = &imageIndex;

	if (waitSemaphore != VK_NULL_HANDLE)
	{
		info.waitSemaphoreCount = 1;
		info.pWaitSemaphores    = &waitSemaphore;
	}

	// Deliberately not VK_CHECK — caller handles suboptimal/out-of-date.
	return vkQueuePresentKHR(m_queue, &info);
}


// Transfer queue
void TransferQueue::InitTimelineSemaphore()
{
	ASSERT(m_logicalDeviceCopy != VK_NULL_HANDLE);
	m_sync = CreateTimelineSemaphore(m_logicalDeviceCopy);
}

void TransferQueue::DestroyTimelineSemaphore()
{
	DestroySemaphore(m_logicalDeviceCopy, m_sync.semaphore);
	m_sync = {};
}

uint64_t TransferQueue::Submit(
	const std::vector<VkCommandBuffer>& cmdBuffers,
	uint64_t                            waitValue,
	VkSemaphore                         waitSemaphore,
	VkPipelineStageFlags2               waitStages)
{
	const uint64_t signaled = SubmitWithTimelineSync(
		cmdBuffers,
		m_sync.semaphore,
		m_sync.signalValue,
		waitSemaphore,
		waitValue,
		waitStages);

	m_sync.signalValue = signaled + 1;
	return signaled;
}

// Compute queue
void ComputeQueue::InitTimelineSemaphore()
{
	ASSERT(m_logicalDeviceCopy != VK_NULL_HANDLE);
	m_sync = CreateTimelineSemaphore(m_logicalDeviceCopy);
}

void ComputeQueue::DestroyTimelineSemaphore()
{
	DestroySemaphore(m_logicalDeviceCopy, m_sync.semaphore);
	m_sync = {};
}

uint64_t ComputeQueue::Submit(
	const std::vector<VkCommandBuffer>& cmdBuffers,
	VkSemaphore                          waitSemaphore,
	uint64_t                             waitValue,
	VkPipelineStageFlags2                waitStages)
{
	const uint64_t signaled = SubmitWithTimelineSync(
		cmdBuffers,
		m_sync.semaphore,
		m_sync.signalValue,
		waitSemaphore,
		waitValue,
		waitStages);

	m_sync.signalValue = signaled + 1;
	return signaled;
}
