#pragma once

#include "../backend/VulkanForward.h"

#include <vector>
#include <cstdint>

class SecondaryCmdArena
{
public:
	void Init(
		VkDevice device,
		uint32_t threadSlotCount,
		uint32_t computeFamilyIndex);

	void Cleanup();

	void BeginFrame();

	// Returns an allocated, NOT yet begun secondary. Caller begins it.
	// Grows in chunks; never shrinks.
	VkCommandBuffer Acquire(uint32_t threadSlot);

	bool IsInitialized() const noexcept { return m_device != VK_NULL_HANDLE; }

	uint32_t GetSlotCount() const noexcept
	{
		return static_cast<uint32_t>(m_slots.size());
	}

private:
	static constexpr uint32_t kGrowChunk = 8u;

	struct ThreadSlot
	{
		VkCommandPool                pool = VK_NULL_HANDLE;
		std::vector<VkCommandBuffer> cmds;
		uint32_t                     next = 0u;
		bool                         bUsedThisFrame = false;
	};

	void GrowSlot(ThreadSlot& slot);

	VkDevice                m_device = VK_NULL_HANDLE;
	std::vector<ThreadSlot> m_slots;
};

