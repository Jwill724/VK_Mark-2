#pragma once

#include "../RenderGraphResources.h"
#include "EngineTypes.h"

constexpr Extents3D WORKGROUP_16x16 { 16u, 16u, 1u };
constexpr Extents3D WORKGROUP_8x8 { 8u, 8u, 1u };
constexpr Extents3D WORKGROUP_256 { 256u, 1u, 1u };
constexpr Extents3D WORKGROUP_1 { 1u, 1u, 1u };
constexpr Extents3D WORKGROUP_NONE { 0u, 0u, 0u };

class PushDescriptorWriter;
struct AllocatedBuffer;

class ComputeScope final : public RenderScope
{
public:
	ComputeScope(
		Extents2D extent,
		Extents3D workgroupSize = WORKGROUP_16x16)
	{
		InitCompute(extent, workgroupSize);
	}

	void UpdateExtent(Extents2D extent) { m_extent = extent; }
	void UpdateWorkgroups(Extents3D size, bool skipAutoGroupComputation = false)
	{
		m_workgroupSize = size;
		if (m_workgroupSize.IsDefined()) m_bSkipGroups = skipAutoGroupComputation;
	}

	const Extents2D& GetDrawExtent() const { return m_extent; }

	void SetIndirect(VkBuffer buffer, VkDeviceSize offset = 0) noexcept
	{
		m_indirect.buffer = buffer;
		m_indirect.offset = offset;
		m_bSkipGroups = true;
	}

	void ClearIndirect() noexcept
	{
		m_indirect = {};
		m_bSkipGroups = false;
	}
	// MUST call SetIndirect() first
	void FillIndirectDispatch(VkCommandBuffer cmd, size_t stride);

	void FillGpuBuffer(VkCommandBuffer cmd, const AllocatedBuffer& buf, uint32_t value = 0u);

	void DispatchComputePass(
		VkCommandBuffer cmd,
		const PipelineHandle& pipeHandle,
		PushDescriptorWriter& pushWriter);

private:
	Extents2D m_extent{ 0u, 0u };
	Extents3D m_workgroupSize = WORKGROUP_16x16;
	uint32_t m_groupCountX = 0u;
	uint32_t m_groupCountY = 0u;
	uint32_t m_groupCountZ = 1u;

	void InitCompute(Extents2D extent, Extents3D workgroupSize)
	{
		m_extent = extent;

		if (!workgroupSize.IsDefined())
		{
			m_bSkipGroups = true;
			return;
		}

		m_workgroupSize = workgroupSize;
	}

	struct DispatchIndirectInfo
	{
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceSize offset = 0;
		bool IsSet() const noexcept { return buffer != VK_NULL_HANDLE; }
	};
	DispatchIndirectInfo m_indirect{};

	bool m_bSkipGroups = false;

	// Called right before vkDispatchCmd
	void CalculateGroups() noexcept
	{
		if (m_bSkipGroups) return;
		m_groupCountX = (m_extent.Width() + m_workgroupSize.Width() - 1u) / m_workgroupSize.Width();
		m_groupCountY = (m_extent.Height() + m_workgroupSize.Height() - 1u) / m_workgroupSize.Height();
		m_groupCountZ = m_workgroupSize.Depth(); // usually 1
	}

	bool IsIndirect() const noexcept { return m_indirect.buffer != VK_NULL_HANDLE; }
};
