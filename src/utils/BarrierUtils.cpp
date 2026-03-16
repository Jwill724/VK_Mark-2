#include "pch.h"

#include "BarrierUtils.h"
#include "renderer/backend/Backend.h"

static inline void resolveFamilies(uint32_t& src, uint32_t& dst, bool concurrent) {
	// If buffer is concurrent OR families match, do NOT encode a QFOT.
	if (concurrent || src == dst) {
		src = VK_QUEUE_FAMILY_IGNORED;
		dst = VK_QUEUE_FAMILY_IGNORED;
	}
}

uint32_t BarrierUtils::queueFamilyIndex(QueueType q) {
	switch (q) {
	case QueueType::Graphics: return Backend::getGraphicsQueue().familyIndex;
	case QueueType::Transfer: return Backend::getTransferQueue().familyIndex;
	case QueueType::Compute:  return Backend::getComputeQueue().familyIndex;
	default: ASSERT(false && "queueFamilyIndex: unknown QueueType"); return VK_QUEUE_FAMILY_IGNORED;
	}
}

void BarrierUtils::bufferComputeWriteToComputeRead(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
	b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.buffer = buf.buffer;
	b.offset = 0;
	b.size = VK_WHOLE_SIZE;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers = &b;

	vkCmdPipelineBarrier2(cmd, &di);
}

void BarrierUtils::bufferComputeWriteToComputeRW(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
	b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.buffer = buf.buffer;
	b.offset = 0;
	b.size = VK_WHOLE_SIZE;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers = &b;

	vkCmdPipelineBarrier2(cmd, &di);
}

void BarrierUtils::bufferComputeWriteToIndirectDispatchRead(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
	b.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
	b.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.buffer = buf.buffer;
	b.offset = 0;
	b.size = VK_WHOLE_SIZE;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers = &b;

	vkCmdPipelineBarrier2(cmd, &di);
}

void BarrierUtils::bufferFillToComputeRW(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.buffer = buf.buffer;
	b.offset = 0;
	b.size = VK_WHOLE_SIZE;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers = &b;

	vkCmdPipelineBarrier2(cmd, &di);
}

void BarrierUtils::bufferTransferWriteToGraphicsRead(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	uint32_t s = queueFamilyIndex(QueueType::Transfer), d = queueFamilyIndex(QueueType::Graphics);
	resolveFamilies(s, d, buf.isConcurrent);

	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
	b.srcAccessMask = VK_ACCESS_2_NONE;
	b.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
	b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
	b.srcQueueFamilyIndex = s;
	b.dstQueueFamilyIndex = d;
	b.buffer = buf.buffer;
	b.offset = 0;
	b.size = VK_WHOLE_SIZE;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers = &b;

	vkCmdPipelineBarrier2(cmd, &di);
}

void BarrierUtils::bufferTransferWriteToIndirectRead(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	uint32_t s = queueFamilyIndex(QueueType::Transfer), d = queueFamilyIndex(QueueType::Graphics);
	resolveFamilies(s, d, buf.isConcurrent);

	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
	b.srcAccessMask = VK_ACCESS_2_NONE;
	b.dstStageMask = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
	b.dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT;
	b.srcQueueFamilyIndex = s;
	b.dstQueueFamilyIndex = d;
	b.buffer = buf.buffer;
	b.offset = 0;
	b.size = VK_WHOLE_SIZE;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers = &b;

	vkCmdPipelineBarrier2(cmd, &di);
}

void BarrierUtils::bufferComputeWriteToFragmentRead(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
	b.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
	b.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
	b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
	b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	b.buffer = buf.buffer;
	b.offset = 0;
	b.size = VK_WHOLE_SIZE;

	VkDependencyInfo di{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	di.bufferMemoryBarrierCount = 1;
	di.pBufferMemoryBarriers = &b;

	vkCmdPipelineBarrier2(cmd, &di);
}

void BarrierUtils::bufferTransferReleaseOnGraphics(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	uint32_t srcFamilyIndex = queueFamilyIndex(QueueType::Transfer);
	uint32_t dstFamilyIndex = queueFamilyIndex(QueueType::Graphics);
	resolveFamilies(srcFamilyIndex, dstFamilyIndex, buf.isConcurrent);

	VkBufferMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
	barrier.dstAccessMask = VK_ACCESS_2_NONE;
	barrier.srcQueueFamilyIndex = srcFamilyIndex;
	barrier.dstQueueFamilyIndex = dstFamilyIndex;
	barrier.buffer = buf.buffer;
	barrier.offset = 0;
	barrier.size = VK_WHOLE_SIZE;

	VkDependencyInfo dependencyInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	dependencyInfo.bufferMemoryBarrierCount = 1;
	dependencyInfo.pBufferMemoryBarriers = &barrier;

	vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}

void BarrierUtils::bufferTransferReleaseOnCompute(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	uint32_t srcFamilyIndex = queueFamilyIndex(QueueType::Transfer);
	uint32_t dstFamilyIndex = queueFamilyIndex(QueueType::Compute);
	resolveFamilies(srcFamilyIndex, dstFamilyIndex, buf.isConcurrent);

	VkBufferMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
	barrier.dstAccessMask = VK_ACCESS_2_NONE;
	barrier.srcQueueFamilyIndex = srcFamilyIndex;
	barrier.dstQueueFamilyIndex = dstFamilyIndex;
	barrier.buffer = buf.buffer;
	barrier.offset = 0;
	barrier.size = VK_WHOLE_SIZE;

	VkDependencyInfo dependencyInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	dependencyInfo.bufferMemoryBarrierCount = 1;
	dependencyInfo.pBufferMemoryBarriers = &barrier;

	vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}

void BarrierUtils::bufferTransferReleaseOnIndirect(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf)
{
	uint32_t srcFamilyIndex = queueFamilyIndex(QueueType::Transfer);
	uint32_t dstFamilyIndex = queueFamilyIndex(QueueType::Graphics);
	resolveFamilies(srcFamilyIndex, dstFamilyIndex, buf.isConcurrent);

	VkBufferMemoryBarrier2 barrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
	barrier.dstAccessMask = VK_ACCESS_2_NONE;
	barrier.srcQueueFamilyIndex = srcFamilyIndex;
	barrier.dstQueueFamilyIndex = dstFamilyIndex;
	barrier.buffer = buf.buffer;
	barrier.offset = 0;
	barrier.size = VK_WHOLE_SIZE;

	VkDependencyInfo dependencyInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
	dependencyInfo.bufferMemoryBarrierCount = 1;
	dependencyInfo.pBufferMemoryBarriers = &barrier;

	vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}
