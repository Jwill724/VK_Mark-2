#include "pch.h"

#include "RenderGraphResources.h"
#include "../backend/VulkanTypes.h"
#include "../backend/DescriptorWriter.h"
#include "../backend/memory/AllocatedImage.h"

void RenderScope::BindPushConstant(VkCommandBuffer cmd, const PipelineHandle& pipeHandle)
{
	if (m_pushData && m_pushSize > 0 && !m_bSkipPushConstant)
	{
		vkCmdPushConstants(
			cmd,
			pipeHandle.layout.pipelineLayout,
			pipeHandle.layout.pushConstantDef.stageFlags,
			pipeHandle.layout.pushConstantDef.offset,
			static_cast<uint32_t>(m_pushSize),
			m_pushData);
	}
}

void* RenderScope::GetValidatedPushData(
	size_t expectedSize,
	size_t expectedAlignment) noexcept
{
	ASSERT(m_pushData != nullptr);
	ASSERT(m_pushSize == expectedSize);

	ASSERT(
		(reinterpret_cast<uintptr_t>(m_pushData) % expectedAlignment) == 0);

	return m_pushData;
}

void RenderScope::BindReadImage(
	PushDescriptorWriter& writer,
	uint32_t binding,
	const AllocatedImage& img,
	VkSampler sampler,
	uint32_t miplevel)
{
	ASSERT(img.IsValid());

	PushLayout layout = img.m_aspect == ImageAspect::Color ? PushLayout::Read : PushLayout::DepthRead;
	writer.WritePushImage(
		binding,
		img,
		layout,
		sampler,
		miplevel);
}

void RenderScope::BindWriteImage(PushDescriptorWriter& writer, uint32_t binding, const AllocatedImage& img, uint32_t storageViewIndex)
{
	ASSERT(img.IsValid());

	writer.WritePushImage(
		binding,
		img,
		PushLayout::Write,
		VK_NULL_HANDLE,
		storageViewIndex);
}
