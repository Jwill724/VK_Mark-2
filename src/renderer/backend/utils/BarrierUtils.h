#pragma once

#include "renderer/backend/VulkanForward.h"

struct AllocatedBuffer;
struct DeviceContext;

namespace BarrierUtils
{
	void BufferComputeWriteToComputeRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);
	void BufferComputeWriteToComputeRW(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	// After an indirect dispatch is preformed
	void BufferComputeWriteToIndirectDispatchRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void BufferFillToComputeRW(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void BufferComputeWriteToFragmentRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void BufferTransferWriteToGraphicsRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		const DeviceContext& dCtx);

	void BufferTransferWriteToIndirectRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		const DeviceContext& dCtx);

	// Transfer queue buffer releases
	void BufferTransferReleaseOnIndirect(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		const DeviceContext& dCtx);
	void BufferTransferReleaseOnCompute(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		const DeviceContext& dCtx);
	void BufferTransferReleaseOnGraphics(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		const DeviceContext& dCtx);
}
