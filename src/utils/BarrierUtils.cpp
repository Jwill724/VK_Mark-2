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

void BarrierUtils::releaseBuffer(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	VkPipelineStageFlags2 srcStage,
	VkAccessFlags2        srcAccess,
	uint32_t              srcFamily,
	uint32_t              dstFamily)
{
	uint32_t s = srcFamily, d = dstFamily;
	resolveFamilies(s, d, buf.isConcurrent);

	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask = srcStage;
	b.srcAccessMask = srcAccess;
	b.dstStageMask = VK_PIPELINE_STAGE_2_NONE; // release
	b.dstAccessMask = 0;
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

void BarrierUtils::acquireBuffer(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	VkPipelineStageFlags2  dstStage,
	VkAccessFlags2         dstAccess,
	uint32_t               srcFamily,
	uint32_t               dstFamily)
{
	uint32_t s = srcFamily, d = dstFamily;
	resolveFamilies(s, d, buf.isConcurrent);

	VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
	b.srcStageMask = VK_PIPELINE_STAGE_2_NONE; // acquire
	b.srcAccessMask = 0;
	b.dstStageMask = dstStage;
	b.dstAccessMask = dstAccess;
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

void BarrierUtils::releaseBufferQ(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	VkPipelineStageFlags2  srcStage,
	VkAccessFlags2         srcAccess,
	QueueType              srcQ,
	QueueType              dstQ)
{
	releaseBuffer(cmd, buf, srcStage, srcAccess,
		queueFamilyIndex(srcQ), queueFamilyIndex(dstQ));
}

void BarrierUtils::acquireBufferQ(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	VkPipelineStageFlags2  dstStage,
	VkAccessFlags2         dstAccess,
	QueueType              srcQ,
	QueueType              dstQ)
{
	acquireBuffer(cmd, buf, dstStage, dstAccess,
		queueFamilyIndex(srcQ), queueFamilyIndex(dstQ));
}

// transfer -> shader read
void BarrierUtils::releaseTransferToShaderReadQ(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	QueueType srcQ,
	QueueType dstQ)
{
	releaseBufferQ(cmd, buf,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		VK_ACCESS_2_TRANSFER_WRITE_BIT,
		srcQ, dstQ);
}

void BarrierUtils::acquireShaderReadQ(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	QueueType srcQ,
	QueueType dstQ)
{
	acquireBufferQ(cmd, buf,
		VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
		VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		VK_ACCESS_2_UNIFORM_READ_BIT | VK_ACCESS_2_SHADER_READ_BIT,
		srcQ, dstQ);
}

// transfer -> indirect
void BarrierUtils::releaseTransferToIndirectQ(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	QueueType srcQ,
	QueueType dstQ)
{
	releaseBufferQ(cmd, buf,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		VK_ACCESS_2_TRANSFER_WRITE_BIT,
		srcQ, dstQ);
}

void BarrierUtils::acquireIndirectQ(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	QueueType srcQ,
	QueueType dstQ)
{
	acquireBufferQ(cmd, buf,
		VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
		VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
		srcQ, dstQ);
}

// transfer -> vertex/index
void BarrierUtils::releaseTransferToVertexIndexQ(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	QueueType srcQ,
	QueueType dstQ)
{
	releaseBufferQ(cmd, buf,
		VK_PIPELINE_STAGE_2_TRANSFER_BIT,
		VK_ACCESS_2_TRANSFER_WRITE_BIT,
		srcQ, dstQ);
}

void BarrierUtils::acquireVertexIndexQ(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	QueueType srcQ,
	QueueType dstQ)
{
	acquireBufferQ(cmd, buf,
		VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
		VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT,
		srcQ, dstQ);
}

// compute producers
void BarrierUtils::releaseComputeWriteQ(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	QueueType srcQ,
	QueueType dstQ)
{
	releaseBufferQ(cmd, buf,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		VK_ACCESS_2_SHADER_WRITE_BIT,
		srcQ, dstQ);
}

void BarrierUtils::releaseComputeToIndirectQ(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	QueueType srcQ,
	QueueType dstQ)
{
	releaseBufferQ(cmd, buf,
		VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
		VK_ACCESS_2_SHADER_WRITE_BIT, // compute fills indirect args
		srcQ, dstQ);
}