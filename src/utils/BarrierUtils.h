#pragma once

#include "common/EngineTypes.h"
#include "common/ResourceTypes.h"

namespace BarrierUtils {
	// === BUFFER BARRIERS ===

	// Map a QueueType to its family index (from Backend)
	uint32_t queueFamilyIndex(QueueType q);

	void releaseBuffer(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		VkPipelineStageFlags2 srcStage,
		VkAccessFlags2        srcAccess,
		uint32_t              srcFamily,
		uint32_t              dstFamily);

	void acquireBuffer(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		VkPipelineStageFlags2  dstStage,
		VkAccessFlags2         dstAccess,
		uint32_t               srcFamily,
		uint32_t               dstFamily);

	void releaseBufferQ(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		VkPipelineStageFlags2  srcStage,
		VkAccessFlags2         srcAccess,
		QueueType              srcQ,
		QueueType              dstQ);

	void acquireBufferQ(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		VkPipelineStageFlags2  dstStage,
		VkAccessFlags2         dstAccess,
		QueueType              srcQ,
		QueueType              dstQ);

	// convenience: transfer writes -> shader reads (uniform/storage)
	void releaseTransferToShaderReadQ(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		QueueType srcQ = QueueType::Transfer,
		QueueType dstQ = QueueType::Graphics);

	void acquireShaderReadQ(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		QueueType srcQ = QueueType::Transfer,
		QueueType dstQ = QueueType::Graphics);

	// convenience: transfer writes -> indirect draw reads
	void releaseTransferToIndirectQ(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		QueueType srcQ = QueueType::Transfer,
		QueueType dstQ = QueueType::Graphics);

	void acquireIndirectQ(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		QueueType srcQ = QueueType::Transfer,
		QueueType dstQ = QueueType::Graphics);

	// convenience: transfer writes -> vertex/index reads
	void releaseTransferToVertexIndexQ(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		QueueType srcQ = QueueType::Transfer,
		QueueType dstQ = QueueType::Graphics);

	void acquireVertexIndexQ(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		QueueType srcQ = QueueType::Transfer,
		QueueType dstQ = QueueType::Graphics);

	// compute producers
	void releaseComputeWriteQ(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		QueueType srcQ = QueueType::Compute,
		QueueType dstQ = QueueType::Graphics);

	void releaseComputeToIndirectQ(
		VkCommandBuffer cmd,
		const AllocatedBuffer& buf,
		QueueType srcQ = QueueType::Compute,
		QueueType dstQ = QueueType::Graphics);
}