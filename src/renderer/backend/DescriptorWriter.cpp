#include <pch.h>

#include "DescriptorWriter.h"
#include "memory/AllocatedImage.h"
#include "memory/AllocatedBuffer.h"
#include "../RendererDefinitions.h"

namespace RD = RendererDefinitions;

static VkImageLayout ResolvePushLayout(PushLayout layout)
{
	switch(layout)
	{
		case PushLayout::Read:
			return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		case PushLayout::Write:
			return VK_IMAGE_LAYOUT_GENERAL;

		case PushLayout::DepthRead:
			return VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;

		default:
			ASSERT(false);
			return VK_IMAGE_LAYOUT_UNDEFINED;
	}
}

// ------------------------
// Push descriptor writing
// ------------------------
void PushDescriptorWriter::WritePushImage(
	uint32_t binding,
	const AllocatedImage& image,
	PushLayout imgLayout,
	VkSampler sampler,
	uint32_t storageViewIndex)
{
	m_bEnablePushDescriptor = true;

	VkDescriptorType type = sampler != VK_NULL_HANDLE ?
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER :
		VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

	ASSERT(image.m_imageView != VK_NULL_HANDLE);

	VkImageView view = image.m_imageView;
	if (storageViewIndex != UINT32_MAX && storageViewIndex < image.m_mipLevels)
		view = image.m_vStorageViews[static_cast<size_t>(storageViewIndex)];

	VkImageLayout layout = ResolvePushLayout(imgLayout);

	m_imageWriteGroups.emplace_back(ImageWriteGroup{
		.binding     = binding,
		.type        = type,
		.dstSet      = VK_NULL_HANDLE,
		.imageInfos  = { {sampler, view, layout } }
	});
}

void PushDescriptorWriter::UpdatePushLayout(
	VkCommandBuffer cmd,
	VkPipelineBindPoint bindPoint,
	VkPipelineLayout pipelineLayout)
{
	if (!m_bEnablePushDescriptor) return;

	ASSERT(pipelineLayout != VK_NULL_HANDLE);

	std::vector<VkWriteDescriptorSet> writes;
	writes.reserve(m_imageWriteGroups.size());

	for (const auto& group : m_imageWriteGroups)
	{
		ASSERT(group.imageInfos.size() == 1);

		writes.emplace_back(VkWriteDescriptorSet{
			.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstBinding      = group.binding,
			.descriptorCount = 1,
			.descriptorType  = group.type,
			.pImageInfo      = group.imageInfos.data()
		});
	}

	vkCmdPushDescriptorSet(
		cmd,
		bindPoint,
		pipelineLayout,
		RD::PUSH_SET,
		static_cast<uint32_t>(writes.size()),
		writes.data());

	Clear();
	m_bEnablePushDescriptor = false;
}

// ---------------------------------
// Bindless descriptor set writing
// ---------------------------------
void DescriptorWriter::WriteBuffer(
	uint32_t binding,
	const AllocatedBuffer& buffer,
	VkDescriptorSet set)
{
	const size_t bufferIndex = m_bufferInfos.size();
	ASSERT(buffer.m_bytesSize != 0);

	m_bufferInfos.emplace_back(buffer.m_buffer, 0, buffer.m_bytesSize);

	VkDescriptorType bufferType{};
	switch(binding)
	{
		case RD::ADDRESS_TABLE_BINDING:
			bufferType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			break;

		case RD::FRAME_BINDING_SCENE:
		case RD::FRAME_BINDING_CSM:
		case RD::FRAME_BINDING_CLUSTERED:
			bufferType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			break;

		default:
			ASSERT(false && "Invalid buffer binding added.");
	}

	m_bufferWrites.emplace_back(VkWriteDescriptorSet{
		.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet          = set,
		.dstBinding      = binding,
		.descriptorCount = 1u,
		.descriptorType  = bufferType,
		.pBufferInfo     = nullptr,
	});

	m_writeBufferIndices.push_back(bufferIndex);
}

void DescriptorWriter::WriteBindlessImages(
	std::span<const VkDescriptorImageInfo> images,
	uint32_t binding,
	VkDescriptorSet set,
	Vulkan_DescriptorType type)
{
	if (images.empty())
		return;

	auto& group = m_imageWriteGroups.emplace_back();

	group.binding = binding;
	group.type = static_cast<VkDescriptorType>(type);
	group.dstSet = set;

	group.imageInfos.assign(
		images.begin(),
		images.end());
}

// Special instant inline set update
void DescriptorWriter::WriteInlineUniform(
	VkDevice device,
	VkDescriptorSet unifiedSet, // Hard defined set
	const void* data,
	uint32_t size)
{
	VkWriteDescriptorSetInlineUniformBlock inlineBlock
	{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK,
		.pNext = nullptr,
		.dataSize = size,
		.pData = data
	};

	VkWriteDescriptorSet write
	{
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.pNext = &inlineBlock,
		.dstSet = unifiedSet,
		.dstBinding = RendererDefinitions::GLOBAL_BINDING_DEBUG_INLINE,
		.descriptorCount = size,
		.descriptorType = VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK
	};

	vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void DescriptorWriter::UpdateSet(VkDevice device, VkDescriptorSet set)
{
	std::vector<VkWriteDescriptorSet> imageWrites;
	if (!m_imageWriteGroups.empty())
	{
		imageWrites.reserve(m_imageWriteGroups.size());
		for (const auto& group : m_imageWriteGroups)
		{
			imageWrites.emplace_back(VkWriteDescriptorSet{
				.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				.dstSet          = group.dstSet,
				.dstBinding      = group.binding,
				.descriptorCount = static_cast<uint32_t>(group.imageInfos.size()),
				.descriptorType  = group.type,
				.pImageInfo      = group.imageInfos.data()
			});
		}

		vkUpdateDescriptorSets(
			device,
			static_cast<uint32_t>(imageWrites.size()),
			imageWrites.data(),
			0,
			nullptr);
		m_bShouldClearWrites = true;
	}

	if (!m_bufferWrites.empty())
	{
		for (size_t i = 0; i < m_bufferWrites.size(); ++i)
		{
			m_bufferWrites[i].dstSet = set;
			m_bufferWrites[i].pBufferInfo = &m_bufferInfos[m_writeBufferIndices[i]];
		}

		vkUpdateDescriptorSets(
			device,
			static_cast<uint32_t>(m_bufferWrites.size()),
			m_bufferWrites.data(),
			0,
			nullptr);
		m_bShouldClearWrites = true;
	}

	if (m_bShouldClearWrites) Clear();
}
