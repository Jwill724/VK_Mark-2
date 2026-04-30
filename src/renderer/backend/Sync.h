#pragma once

#include "VulkanForward.h"
#include "vector"

class FencePool
{
public:
	VkFence GetFence(VkDevice device);

	void Recycle(VkFence fence);

	bool IsFenceReady(VkFence fence, VkDevice device) const;

	void ResetAll(VkDevice device);

	void DestroyFences(VkDevice device);

private:
	std::vector<VkFence> m_availableFences;
	std::vector<VkFence> m_inFlightFences;
};
