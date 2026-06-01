#pragma once

#include "renderer/backend/VulkanForward.h"

struct AllocatedBuffer;
struct DeviceContext;

namespace BufferBarriers
{
	void ComputeWriteToRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);
	void ComputeWriteToRW(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	// After an indirect dispatch is preformed
	void ComputeWriteToIndirectDispatchRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void CmdFillToComputeRW(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void ComputeWriteToFragmentRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void TransferWriteToGraphicsRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		const DeviceContext& dCtx);

	void TransferWriteToIndirectRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		const DeviceContext& dCtx);

	// Transfer queue buffer releases
	void TransferReleaseOnIndirect(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		const DeviceContext& dCtx);
	void TransferReleaseOnCompute(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		const DeviceContext& dCtx);
	void TransferReleaseOnGraphics(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		const DeviceContext& dCtx);
}
