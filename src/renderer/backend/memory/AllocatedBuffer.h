#pragma once

#include "VmaForward.h"
#include <renderer/backend/VulkanForward.h>
#include <string>

struct AllocatedBuffer
{
	VkBuffer              m_buffer        = VK_NULL_HANDLE;
	uint64_t              m_address       = 0;
	VmaAllocation         m_allocation    = nullptr;
	void*                 m_mappedPtr     = nullptr;
	size_t                m_bytesSize     = 0;

	bool                  m_bIsConcurrent = false;
	uint8_t               m_qmask         = 0;

	std::string           n_name;

	bool IsValid()  const noexcept { return m_buffer != VK_NULL_HANDLE; }
	void Reset() { *this = AllocatedBuffer{}; }

	// Maybe put this function somewhere else?
	static size_t AlignUp(size_t value, size_t alignment) noexcept
	{
		return (value + alignment - 1) & ~(alignment - 1);
	}
};
