#include "pch.h"

#include "ComputeScope.h"
#include "../../backend/DescriptorWriter.h"
#include "../../backend/memory/AllocatedBuffer.h"

void ComputeScope::DispatchComputePass(
	VkCommandBuffer cmd,
	const PipelineHandle& pipeHandle,
	PushDescriptorWriter& pushWriter)
{
	vkCmdBindPipeline(cmd, pipeHandle.bindPoint, pipeHandle.pipeline);

	BindPushConstant(cmd, pipeHandle);

	pushWriter.UpdatePushLayout(
		cmd,
		pipeHandle.bindPoint,
		pipeHandle.layout.pipelineLayout);

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

void ComputeScope::FillGpuBuffer(VkCommandBuffer cmd, const AllocatedBuffer& buf, uint32_t value)
{
	vkCmdFillBuffer(
		cmd,
		buf.m_buffer,
		0u,
		buf.m_bytesSize,
		value);
}

void ComputeScope::FillIndirectDispatch(VkCommandBuffer cmd, size_t stride)
{
	ASSERT(m_indirect.IsSet());
	vkCmdFillBuffer(
		cmd,
		m_indirect.buffer,
		m_indirect.offset,
		stride,
		0u);
}
