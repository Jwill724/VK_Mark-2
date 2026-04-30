#pragma once

#include <renderer/backend/VulkanTypes.h>

struct AllocatedImage;
struct AllocatedBuffer;

class DescriptorWriter
{
public:
	void WriteBuffer(
		uint32_t binding,
		const AllocatedBuffer& buffer,
		VkDescriptorSet set);

	void WriteBindlessImages(
		const std::vector<VkDescriptorImageInfo>& images,
		uint32_t binding,
		VkDescriptorSet set,
		Vulkan_DescriptorType type = Vulkan_DescriptorType::COMBINED_SAMPLER); // No storage for now

	// Requires immediate update
	void WriteInlineUniform(
		VkDevice device,
		VkDescriptorSet unifiedSet,
		const void* data,
		uint32_t size);

	void UpdateSet(VkDevice device, VkDescriptorSet set);

	virtual ~DescriptorWriter() { Clear(); }
	void Clear() { ClearImpl(); }

protected:
	// Can write bindless and push images
	struct ImageWriteGroup
	{
		uint32_t binding = UINT32_MAX;
		VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
		VkDescriptorSet dstSet = VK_NULL_HANDLE;

		std::vector<VkDescriptorImageInfo> vImageInfos;  // Bindless array
		VkDescriptorImageInfo imageInfo{};               // Push usage
	};

	// Per-binding grouped image descriptor writes
	std::vector<ImageWriteGroup> m_imageWriteGroups;

private:
	std::vector<VkDescriptorBufferInfo> m_bufferInfos;
	std::vector<VkWriteDescriptorSet> m_bufferWrites;
	std::vector<size_t> m_writeBufferIndices;

	// After frameSet is updated, all data written is cleared
	bool m_bShouldClearWrites = false;

	virtual void ClearImpl()
	{
		m_imageWriteGroups.clear();
		m_bufferWrites.clear();
		m_writeBufferIndices.clear();
		m_bufferInfos.clear();
		m_bShouldClearWrites = false;
	}
};

class PushDescriptorWriter final : public DescriptorWriter
{
public:
	// sampler == read only
	// !sampler == write only
	// Storage index and override layout meant for accessing mip layers
	void WritePushImage(
		uint32_t binding,
		const AllocatedImage& image,
		VkSampler sampler = VK_NULL_HANDLE,
		VkImageLayout overrideLayout = VK_IMAGE_LAYOUT_MAX_ENUM,
		uint32_t storageViewIndex = UINT32_MAX);

	void UpdatePushLayout(
		VkCommandBuffer cmd,
		VkPipelineBindPoint bindPoint,
		VkPipelineLayout pipelineLayout);

private:
	bool m_bEnablePushDescriptor = false;

	void ClearImpl() override
	{
		m_imageWriteGroups.clear();
		m_bEnablePushDescriptor = false;
	}
};
