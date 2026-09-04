#include "pch.h"

#include "RenderGraphResources.h"
#include "../backend/VulkanTypes.h"
#include "../backend/descriptors/DescriptorWriter.h"
#include "../backend/memory/AllocatedImage.h"
#include "../backend/pipelines/PipelineManager.h"

void RenderScope::BindPushConstant(VkCommandBuffer cmd, const PipelineHandle& pipeHandle)
{
	if (m_pushSize > 0 && !m_bSkipPushConstant)
	{
		vkCmdPushConstants(
			cmd,
			pipeHandle.layout.pipelineLayout,
			pipeHandle.layout.pushConstantDef.stageFlags,
			pipeHandle.layout.pushConstantDef.offset,
			static_cast<uint32_t>(m_pushSize),
			m_pushData.data());
	}
}

void* RenderScope::GetValidatedPushData(
	size_t expectedSize,
	size_t expectedAlignment) noexcept
{
	ASSERT(m_pushSize > 0 && "EditPush before SetPush");
	ASSERT(m_pushSize == expectedSize);

	ASSERT(expectedAlignment <= PUSH_ALIGNMENT);
	ASSERT((reinterpret_cast<uintptr_t>(m_pushData.data()) % expectedAlignment) == 0);

	return m_pushData.data();
}

void RenderScope::BindReadImage(
	PushDescriptorWriter& writer,
	uint32_t binding,
	const AllocatedImage& img,
	VkSampler sampler,
	uint32_t miplevel,
	RD::ImageAccess declaredAccess)
{
	ASSERT(img.IsValid());

	const VkImageLayout layout = GetImageSyncScope(declaredAccess).layout;

	ASSERT((img.m_aspect == ImageAspect::Color)
		? (layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		: (layout == VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL)
		&& "Declared ImageAccess does not match this image's aspect");

	writer.WritePushImage(binding, img, layout, sampler, miplevel);
}

void RenderScope::BindWriteImage(
	PushDescriptorWriter& writer,
	uint32_t binding,
	const AllocatedImage& img,
	uint32_t storageViewIndex,
	RD::ImageAccess declaredAccess)
{
	ASSERT(img.IsValid());

	const VkImageLayout layout = GetImageSyncScope(declaredAccess).layout;

	ASSERT(layout == VK_IMAGE_LAYOUT_GENERAL
		&& "Storage image bindings require a GENERAL-layout access "
		   "(Write or ComputeWrite)");

	writer.WritePushImage(binding, img, layout, VK_NULL_HANDLE, storageViewIndex);
}