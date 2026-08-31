#pragma once

#include "VulkanTypes.h"
#include "memory/AllocatedImage.h"
#include "memory/AllocatedBuffer.h"

#include <vector>

struct SceneInfo;
class Device;
class Allocator;
class BindlessImageTable;
class ComputeScope;
class PushDescriptorWriter;

namespace nrd { struct Instance; struct ResourceDesc; }

inline constexpr RD::ImageAccess NRD_INPUT_ACCESS = RD::ImageAccess::ComputeReadStorage;
inline constexpr RD::ImageAccess NRD_OUTPUT_ACCESS = RD::ImageAccess::ComputeWrite;

class NRDContext
{
	friend class Renderer;
public:
	static constexpr uint32_t SPEC_ID = 0u;
	static constexpr uint32_t SHADOW_ID = 1u;
	static constexpr uint32_t RING_FRAMES = RD::MAX_FRAMES_IN_FLIGHT;
	static constexpr uint32_t MAX_CB_DISPATCHES = 64u;

	enum class DenoiserMode : uint32_t { Reflections, Shadows };

	DenoiserMode GetMode() const noexcept { return m_mode; }

	void SetFrameSettings(
		const SceneInfo& scene,
		const BindlessImageTable& imageTable,
		float deltaSeconds,
		bool historyValid);

	void RecordDispatches(
		VkCommandBuffer cmd,
		ComputeScope& scope,
		PushDescriptorWriter& writer) const;

	bool IsValid() const noexcept { return m_instance != nullptr; }
	Extents2D GetExtent() const noexcept { return m_extent; }

private:
	struct IOViews
	{
		VkImageView motion = VK_NULL_HANDLE;
		VkImageView normalRoughness = VK_NULL_HANDLE;
		VkImageView viewZ = VK_NULL_HANDLE;
		VkImageView specRadianceIn = VK_NULL_HANDLE;
		VkImageView specRadianceOut = VK_NULL_HANDLE;
		VkImageView penumbra = VK_NULL_HANDLE;
		VkImageView shadowOut = VK_NULL_HANDLE;
	};

	void Init(const Device& device, Allocator& allocator, Extents2D extent, DenoiserMode mode);
	void Resize(Allocator& allocator, Extents2D extent);
	void Shutdown(VkDevice device, Allocator& allocator);

	void CreateSamplers(VkDevice device);
	void CreateLayoutsAndPipelines(VkDevice device);
	void CreateConstantRing(const Device& device, Allocator& allocator);
	void CreatePools(Allocator& allocator, Extents2D extent);
	void DestroyPools(Allocator& allocator);

	void RecordPoolInit(VkCommandBuffer cmd);

	bool m_bAccumulationReset = false;

	static constexpr VkImageLayout NRD_LAYOUT = VK_IMAGE_LAYOUT_GENERAL;
	static_assert(GetImageSyncScope(NRD_INPUT_ACCESS).layout == NRDContext::NRD_LAYOUT);
	static_assert(GetImageSyncScope(NRD_OUTPUT_ACCESS).layout == NRDContext::NRD_LAYOUT);

	VkImageView ResolveView(const nrd::ResourceDesc& res) const;

	nrd::Instance* m_instance = nullptr;

	std::vector<PipelineHandle>        m_pipelines;
	std::vector<VkSampler>             m_samplers;

	VkDescriptorSetLayout m_cbSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool      m_cbPool = VK_NULL_HANDLE;
	VkDescriptorSet       m_cbSet = VK_NULL_HANDLE;

	std::vector<VkDescriptorSetLayout> m_resourceSetLayouts;

	uint32_t m_setCount = 1u;
	uint32_t m_cbSetIndex = 0u;
	uint32_t m_resourceSetIndex = 0u;

	std::vector<AllocatedImage> m_permanentPool;
	std::vector<AllocatedImage> m_transientPool;

	AllocatedBuffer m_constantRing;
	uint32_t        m_constantStride = 0u;
	uint32_t        m_constantSliceBytes = 0u;

	VkDevice     m_device = VK_NULL_HANDLE;
	VmaAllocator m_vma = VK_NULL_HANDLE;
	Extents2D    m_extent{};

	IOViews   m_io{};
	uint32_t  m_frameSlot = 0u;
	glm::mat4 m_prevProjUnjittered{ 1.0f };

	float m_smoothedDeltaSeconds = 0.0f;

	uint32_t m_maxPushDescriptors = 0u;

	uint32_t m_nrdFrameIndex = 0;

	DenoiserMode m_mode = DenoiserMode::Reflections;

	mutable uint32_t m_ringCursor = 0u;
	mutable uint32_t m_lastCBOffset = 0u;
};
