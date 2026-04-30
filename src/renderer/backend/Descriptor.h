#pragma once

#include <renderer/RendererDefinitions.h>
#include <renderer/backend/VulkanTypes.h>
#include <span>

// https://vkguide.dev/docs/new_chapter_4/descriptor_abstractions/
// The core influence of my descriptor allocation setup, a great starting point

namespace RD = RendererDefinitions;

class DescriptorManager final
{
public:
	// Called before pipeline initialization
	void InitDescriptors(VkDevice device);

	// Call before pipeline destruction
	void CleanupDescriptors(VkDevice device);

	// Called per frame context at init
	VkDescriptorSet AllocateFrameDescriptorSet(VkDevice device);

	VkDescriptorSet GetGlobalSet() const noexcept
	{
		return m_descriptorDefs[static_cast<size_t>(RD::GLOBAL_SET)].descriptorSet;
	}
	VkDescriptorSetLayout GetDescriptorLayout(RD::DescriptorSlot id) const noexcept
	{
		return m_descriptorDefs[static_cast<size_t>(id)].descriptorLayout;
	}

	// Only used once for pipeline setup
	std::vector<VkDescriptorSetLayout> GetDescriptorLayouts() const
	{
		std::vector<VkDescriptorSetLayout> layouts;
		layouts.reserve(m_descriptorDefs.size());
		for (const auto& d : m_descriptorDefs)
			layouts.push_back(d.descriptorLayout);

		return layouts;
	}

	void BindDescriptorSets(
		VkCommandBuffer cmd,
		VkDescriptorSet frameSet,
		const PipelineLayoutConst& globalLayout);

private:
	struct PoolSizeRatio
	{
		Vulkan_DescriptorType type;
		float ratio = 0.0f;
	};

	std::vector<VkDescriptorSetLayoutBinding> m_bindings;

	std::vector<PoolSizeRatio> m_ratios;
	std::vector<VkDescriptorPool> m_fullPools;
	std::vector<VkDescriptorPool> m_readyPools;
	uint32_t m_setsPerPool = 0;

	std::array<DescriptorDef, static_cast<size_t>(RD::DescriptorSlot::Count)> m_descriptorDefs;

	VkDescriptorPool GetPool(VkDevice device);
	void DestroyPools(VkDevice device);
	VkDescriptorPool CreateDescriptorPool(VkDevice device, uint32_t setCount, std::span<PoolSizeRatio> poolRatios);
	VkDescriptorSetLayout CreateSetLayout(VkDevice device);
	VkDescriptorSet AllocateDescriptor(VkDevice device, VkDescriptorSetLayout layout,
		void* pNext = nullptr, uint32_t count = 1, bool useVariableCount = false);
	void ClearPools(VkDevice device);
	void AddBinding(uint32_t binding, Vulkan_DescriptorType type, Vulkan_ShaderStage stage, uint32_t count = 1u);
	void ClearBinding();

	VkDescriptorSetLayout CreatePushSetLayout(VkDevice device);

	void InitSetPools(VkDevice device, uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);
};
