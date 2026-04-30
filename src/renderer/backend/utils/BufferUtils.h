#pragma once

#include "renderer/RendererDefinitions.h"
#include "renderer/backend/memory/VmaForward.h"
#include "renderer/backend/VulkanForward.h"

using namespace RendererDefinitions;

struct AllocatedBuffer;
class BindlessBufferTable;

namespace BufferUtils
{
	// =========================
	// === BUFFER ALLOCATION ===

	// Creates generic storage buffer
	AllocatedBuffer CreateBuffer(
		size_t allocSize,
		VkFlags usage,
		VmaMemoryUsage memoryUsage,
		const VmaAllocator allocator,
		bool concurrentSharingOn = false);

	// Stores pointer into address table array
	// Positions in array are predefined
	AllocatedBuffer CreateGPUAddressBuffer(
		Renderer_Buffer bufferSlot,
		BindlessBufferTable& bufferTable,
		size_t size,
		const VmaAllocator allocator);

	// Takes abitrary type
	// Only pass in proper 16 byte alignment structs
	template<typename UniformType>
	AllocatedBuffer CreateUniformBuffer(
		const UniformType& type,
		const VmaAllocator allocator);

	// Staging gpu copy commands
	AllocatedBuffer CreateGPUStagingBuffer(
		size_t size,
		const VmaAllocator allocator);

	// ==========================
	// === BUFFER DESTRUCTION ===

	// For more discrete types where data reset occurs
	void DestroyAllocatedBuffer(
		AllocatedBuffer& buffer,
		const VmaAllocator allocator);

	// Temporary by value destruction when out of scope
	void DestroyBuffer(
		VkBuffer buffer,
		VmaAllocation allocation,
		const VmaAllocator allocator);

	// =====================
	// === BUFFER RANGES ===

	// Staging buffer helpers
	size_t ReserveStaging(
		size_t& stagingHead,
		size_t totalStagingSize,
		size_t stageBytes);
	size_t AlignUp(size_t x, size_t a);

	// Flush a written host range
	void FlushStagingRange(
		const VmaAllocation bufAllocation,
		size_t offset,
		size_t bytes,
		const VmaAllocator allocator);
}
