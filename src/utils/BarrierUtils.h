#pragma once

#include "common/EngineTypes.h"
#include "common/ResourceTypes.h"

namespace BarrierUtils {
	// === BUFFER BARRIERS ===

	// Map a QueueType to its family index (from Backend)
	uint32_t queueFamilyIndex(QueueType q);

	void bufferComputeWriteToComputeRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);
	void bufferComputeWriteToComputeRW(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	// After an indirect dispatch is preformed
	void bufferComputeWriteToIndirectDispatchRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void bufferFillToComputeRW(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void bufferComputeWriteToFragmentRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void bufferTransferWriteToGraphicsRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void bufferTransferWriteToIndirectRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	// Transfer queue buffer releases
	void bufferTransferReleaseOnIndirect(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);
	void bufferTransferReleaseOnCompute(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);
	void bufferTransferReleaseOnGraphics(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);
}
