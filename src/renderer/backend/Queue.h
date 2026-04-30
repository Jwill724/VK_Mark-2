#pragma once

#include "VulkanTypes.h"
#include "ResourceTypes.h"
#include <mutex>
#include <span>
#include "Sync.h"

class FencePool;
struct DeviceContext;

// GPUQueue is like the arbitar of sync tooling.
// Semaphore and fence management.
class GPUQueue
{
public:
	uint32_t GetFamilyIndex() const { return m_familyIndex; }
	VkQueue  GetQueue() const { return m_queue; }

	GPUQueue() = default;
	GPUQueue(uint32_t familyIndex, uint32_t timestampBits, QueueType qType)
		: m_familyIndex(familyIndex)
		, m_timestampValidBits(timestampBits)
		, m_qType(qType) {}

	void GetDeviceQueue(const DeviceContext& deviceCtx);

	void WaitIdle() const;

	// Only the transfer and compute queue need this
	uint64_t SubmitWithTimelineSync(
		const std::vector<VkCommandBuffer>& cmdBuffers,
		VkSemaphore                         timelineSemaphore,
		uint64_t                            signalValue,
		VkSemaphore                         waitSemaphore = VK_NULL_HANDLE,
		uint64_t                            waitValue     = 0,
		VkPipelineStageFlags2               waitStages    = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
	void WaitTimelineValue(VkSemaphore semaphore, uint64_t waitValue);

	void SetTimestampBits(uint32_t timestampBits) { m_timestampValidBits = timestampBits; }

	void SubmitCommand(VkCommandBuffer command);
	void SubmitCommand(std::vector<VkCommandBuffer> commands);

	void CleanupFencePools();

	static VkSemaphore   CreateNewSemaphore(VkDevice device);
	static VkFence       CreateNewFence(VkDevice device);
	static void          DestroyFence(VkDevice device, VkFence fence);
	static void          DestroySemaphore(VkDevice device, VkSemaphore semaphore);

protected:
	TimelineSync CreateTimelineSemaphore(VkDevice device);

	VkFence SubmitInfo(const VkSubmitInfo& info);
	VkFence SubmitInfo(const std::vector<VkSubmitInfo>& infos);
	void    WaitAndRecycleLastFence(VkFence& fence);

	mutable std::mutex m_mutex;
	VkQueue            m_queue              = VK_NULL_HANDLE;
	VkDevice           m_logicalDeviceCopy  = VK_NULL_HANDLE; // Copied between all queues
	FencePool          m_fencePool;
	uint32_t           m_familyIndex        = UINT32_MAX;
	QueueType          m_qType              = QueueType::Nothing;
	uint32_t           m_timestampValidBits = 0;
};

class GraphicsQueue final : public GPUQueue
{
public:
	bool SupportsTimestamps() const noexcept { return m_timestampValidBits > 0; }

	// Caller builds waitInfos (image-available + any timeline waits),
	// queue owns the vkQueueSubmit2 call.
	void SubmitFrame(
		const std::vector<VkSemaphoreSubmitInfo>& waitInfos,
		VkCommandBuffer                           cmdBuffer,
		VkSemaphore                               signalSemaphore,
		VkFence                                   fence);

	// Returns false if any query was VK_NOT_READY — caller retries next frame.
	struct TimestampResult { float gpuMs = 0.f; bool valid = false; };

	bool ReadTimestamps(
		VkQueryPool                         pool,
		std::span<const PassTimestampRange> ranges,
		std::span<const bool>               passUsed,
		float                               timestampPeriod,
		std::span<TimestampResult>          outResults);
};


class PresentQueue final : public GPUQueue
{
public:
	// Returns VK_SUCCESS, VK_SUBOPTIMAL_KHR, or VK_ERROR_OUT_OF_DATE_KHR.
	// Deliberately not VK_CHECK — caller handles swapchain recreation.
	VkResult Present(
		VkSwapchainKHR swapchain,
		uint32_t       imageIndex,
		VkSemaphore    waitSemaphore = VK_NULL_HANDLE);
};


class TransferQueue final : public GPUQueue
{
public:
	// Set device first
	void InitTimelineSemaphore();
	void DestroyTimelineSemaphore();

	// Submit work and advance the internal timeline.
	uint64_t Submit(
		const std::vector<VkCommandBuffer>& cmdBuffers,
		VkSemaphore                         waitSemaphore = VK_NULL_HANDLE,
		uint64_t                            waitValue     = 0,
		VkPipelineStageFlags2               waitStages    = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);

	// The semaphore callers need to embed in their wait lists.
	VkSemaphore GetTimelineSemaphore() const { return m_sync.semaphore; }

	// Current signal value — compare against your stored waitValue to guard submits.
	uint64_t GetCurrentSignalValue() const { return m_sync.signalValue; }

private:
	TimelineSync m_sync;
};

class ComputeQueue final : public GPUQueue
{
public:
	void InitTimelineSemaphore();
	void DestroyTimelineSemaphore();

	uint64_t Submit(
		const std::vector<VkCommandBuffer>& cmdBuffers,
		VkSemaphore                         waitSemaphore = VK_NULL_HANDLE,
		uint64_t                            waitValue     = 0,
		VkPipelineStageFlags2               waitStages    = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);

	VkSemaphore GetTimelineSemaphore() const { return m_sync.semaphore; }
	uint64_t    GetCurrentSignalValue() const { return m_sync.signalValue; }

private:
	TimelineSync m_sync;
};
