#include "pch.h"

#include "ComputeScope.h"
#include "../../backend/DescriptorWriter.h"
#include "../../backend/memory/AllocatedBuffer.h"

void ComputeScope::DispatchComputePass(
	VkCommandBuffer cmd,
	const PipelineHandle& pipeHandle,
	PushDescriptorWriter& pushWriter,
	uint32_t pushSetIndex)
{
	vkCmdBindPipeline(cmd, pipeHandle.bindPoint, pipeHandle.pipeline);

	BindPushConstant(cmd, pipeHandle);

	pushWriter.UpdatePushLayout(
		cmd,
		pipeHandle.bindPoint,
		pipeHandle.layout.pipelineLayout,
		pushSetIndex);

	if (IsIndirect())
	{
		vkCmdDispatchIndirect(
			cmd,
			m_indirect.buffer,
			m_indirect.offset);
		return;
	}

	CalculateGroups();

	vkCmdDispatch(
		cmd,
		m_groupCountX,
		m_groupCountY,
		m_groupCountZ);
}

void ComputeScope::FillGpuBuffer(
	VkCommandBuffer cmd,
	const AllocatedBuffer& buf,
	uint32_t value,
	VkDeviceSize offset,
	VkDeviceSize size)
{
	if (size == VK_WHOLE_SIZE)
		size = buf.m_bytesSize - offset;

	vkCmdFillBuffer(cmd, buf.m_buffer, offset, size, value);
}
