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
	void ComputeWriteToTransferRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void ComputeWriteToIndirectRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void CmdFillToComputeRW(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void ComputeWriteToFragmentRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void ComputeWriteToVertexRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf);

	void TransferWriteToComputeRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		const DeviceContext& dCtx);

	void TransferWriteToGraphicsRead(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		const DeviceContext& dCtx);

	// Transfer queue buffer releases
	void TransferReleaseOnCompute(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		const DeviceContext& dCtx);
	void TransferReleaseOnGraphics(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		const DeviceContext& dCtx);
}
