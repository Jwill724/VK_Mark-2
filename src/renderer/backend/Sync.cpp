#include "pch.h"

#include "Sync.h"

VkFence FencePool::GetFence(VkDevice device)
{
	if (!m_availableFences.empty())
	{
		VkFence fence = m_availableFences.back();
		m_availableFences.pop_back();
		VK_CHECK(vkResetFences(device, 1, &fence));
		m_inFlightFences.push_back(fence);
		return fence;
	}

	VkFenceCreateInfo info{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	VkFence fence;
	VK_CHECK(vkCreateFence(device, &info, nullptr, &fence));
	m_inFlightFences.push_back(fence);
	return fence;
}

void FencePool::Recycle(VkFence fence)
{
	m_availableFences.push_back(fence);
	m_inFlightFences.erase(std::remove(m_inFlightFences.begin(), m_inFlightFences.end(), fence), m_inFlightFences.end());
}

bool FencePool::IsFenceReady(VkFence fence, VkDevice device) const
{
	return vkGetFenceStatus(device, fence) == VK_SUCCESS;
}

void FencePool::ResetAll(VkDevice device)
{
	for (auto& fence : m_inFlightFences)
	{
		VK_CHECK(vkResetFences(device, 1, &fence));
		m_availableFences.push_back(fence);
	}
	m_inFlightFences.clear();
}

void FencePool::DestroyFences(VkDevice device)
{
	for (auto& fence : m_availableFences)
		vkDestroyFence(device, fence, nullptr);
	for (auto& fence : m_inFlightFences)
		vkDestroyFence(device, fence, nullptr);
	m_availableFences.clear();
	m_inFlightFences.clear();
}
