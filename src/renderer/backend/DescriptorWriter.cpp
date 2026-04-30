#include <pch.h>

#include "DescriptorWriter.h"
#include "resources/AllocatedImage.h"
#include "resources/AllocatedBuffer.h"

// ------------------------
// Push descriptor writing
// ------------------------
void PushDescriptorWriter::WritePushImage(
	uint32_t binding,
	const AllocatedImage& image,
	VkSampler sampler,
	VkImageLayout overrideLayout,
	uint32_t storageViewIndex)
{
	m_bEnablePushDescriptor = true;

	VkDescriptorType type = sampler != VK_NULL_HANDLE ?
		VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER :
		VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;

	VkImageView view = image.imageView;
	if (storageViewIndex != UINT32_MAX && storageViewIndex < image.mipLevelCount)
		view = image.storageViews[static_cast<size_t>(storageViewIndex)];

	VkImageLayout layout = image.currentLayout;
	if (overrideLayout != VK_IMAGE_LAYOUT_MAX_ENUM)
		layout = overrideLayout;

	m_imageWriteGroups.emplace_back(ImageWriteGroup{
		.binding   = binding,
		.type      = type,
		.dstSet    = VK_NULL_HANDLE,
		.imageInfo = { sampler, view, layout }
	});
}

void PushDescriptorWriter::UpdatePushLayout(
	VkCommandBuffer cmd,
	VkPipelineBindPoint bindPoint,
	VkPipelineLayout pipelineLayout)
{
	if (!m_bEnablePushDescriptor) return;

	std::vector<VkWriteDescriptorSet> writes;
	writes.reserve(m_imageWriteGroups.size());

	for (const auto& group : m_imageWriteGroups)
	{
		writes.emplace_back(VkWriteDescriptorSet{
			.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
			.dstSet           = VK_NULL_HANDLE,
			.dstBinding       = group.binding,
			.descriptorCount  = 1u,
			.descriptorType   = group.type,
			.pImageInfo       = &group.imageInfo
		});
	}

	vkCmdPushDescriptorSet(
		cmd,
		bindPoint,
		pipelineLayout,
		RendererDefinitions::PUSH_SET, // Hard coded frameSet 2
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

	m_bufferInfos.emplace_back(buffer.m_buffer, 0, buffer.m_bytesSize);

	m_bufferWrites.emplace_back(VkWriteDescriptorSet{
		.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet          = set,
		.dstBinding      = binding,
		.descriptorCount = 1u,
		.descriptorType  = static_cast<VkDescriptorType>(buffer.m_type),
		.pBufferInfo     = nullptr,
	});

	m_writeBufferIndices.push_back(bufferIndex);
}

void DescriptorWriter::WriteBindlessImages(
	const std::vector<VkDescriptorImageInfo>& images,
	uint32_t binding,
	VkDescriptorSet set,
	Vulkan_DescriptorType type)
{
	if (images.empty()) return;
	m_imageWriteGroups.emplace_back(ImageWriteGroup{
		.binding      = binding,
		.type         = static_cast<VkDescriptorType>(type),
		.dstSet       = set,
		.vImageInfos = images
	});
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
				.descriptorCount = static_cast<uint32_t>(group.vImageInfos.size()),
				.descriptorType  = group.type,
				.pImageInfo      = group.vImageInfos.data()
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
