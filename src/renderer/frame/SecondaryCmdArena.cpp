#include "pch.h"

#include "SecondaryCmdArena.h"
#include "../backend/VulkanTypes.h"

void SecondaryCmdArena::Init(
	VkDevice device,
	uint32_t threadSlotCount,
	uint32_t computeFamilyIndex)
{
	ASSERT(device != VK_NULL_HANDLE);
	ASSERT(threadSlotCount > 0u);
	ASSERT(m_slots.empty() && "SecondaryCmdArena::Init called twice");

	m_device = device;
	m_slots.resize(threadSlotCount);

	VkCommandPoolCreateInfo info{};
	info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	info.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	info.queueFamilyIndex = computeFamilyIndex;

	for (ThreadSlot& slot : m_slots)
	{
		VK_CHECK(vkCreateCommandPool(m_device, &info, nullptr, &slot.pool));
	}
}

void SecondaryCmdArena::Cleanup()
{
	if (m_device == VK_NULL_HANDLE) return;

	for (ThreadSlot& slot : m_slots)
	{
		if (slot.pool != VK_NULL_HANDLE)
		{
			vkDestroyCommandPool(m_device, slot.pool, nullptr);
		}
		slot = ThreadSlot{};
	}

	m_slots.clear();
	m_device = VK_NULL_HANDLE;
}

void SecondaryCmdArena::BeginFrame()
{
	for (ThreadSlot& slot : m_slots)
	{
		if (!slot.bUsedThisFrame)
			continue;

		VK_CHECK(vkResetCommandPool(m_device, slot.pool, 0));
		slot.next = 0u;
		slot.bUsedThisFrame = false;
	}
}

void SecondaryCmdArena::GrowSlot(ThreadSlot& slot)
{
	const size_t oldSize = slot.cmds.size();
	slot.cmds.resize(oldSize + kGrowChunk);

	VkCommandBufferAllocateInfo alloc{};
	alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	alloc.commandPool        = slot.pool;
	alloc.level              = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
	alloc.commandBufferCount = kGrowChunk;

	VK_CHECK(vkAllocateCommandBuffers(m_device, &alloc, slot.cmds.data() + oldSize));
}

VkCommandBuffer SecondaryCmdArena::Acquire(uint32_t threadSlot)
{
	ASSERT(m_device != VK_NULL_HANDLE);

	ASSERT(threadSlot < m_slots.size());

	ThreadSlot& slot = m_slots[threadSlot];

	if (slot.next >= slot.cmds.size())
		GrowSlot(slot);

	slot.bUsedThisFrame = true;
	return slot.cmds[slot.next++];
}
