#pragma once

#include "renderer/RendererDefinitions.h"
#include "renderer/backend/VulkanTypes.h"

class PushDescriptorWriter;
struct PipelineHandle;
struct IndirectDrawRange;

class RenderScope
{
public:
	virtual ~RenderScope() = default;
	RendererDefinitions::Renderer_Pass GetPass() const noexcept { return m_pass; }

	template<typename T>
	void SetPush(T& data) noexcept;

	void ClearPush() noexcept;

	template <typename T, typename Fn>
	void EditPush(Fn&& fn) noexcept;

	void BindPushConstant(VkCommandBuffer cmd, const PipelineHandle& handle);

protected:
	RendererDefinitions::Renderer_Pass m_pass = RendererDefinitions::Renderer_Pass::None;
	bool m_bSkipPushConstant = false;
	void* m_pushData = nullptr;
	size_t m_pushSize = 0;

	void InitBase(RendererDefinitions::Renderer_Pass pass) { m_pass = pass; }
};

class GraphicsScope final : public RenderScope
{
public:
	GraphicsScope(
		RendererDefinitions::Renderer_Pass pass,
		VkExtent2D extent,
		const std::vector<AttachmentDesc>& attachments,
		bool isAtlas = false)
	{
		this->InitBase(pass);
		InitGraphics(extent, attachments, isAtlas);
	}

	void UpdateAtlas(VkOffset2D offset, VkExtent2D extent) noexcept
	{
		m_atlasOffset = offset;
		m_atlasExtent = extent;
	}

	// Updates during swapchain resizes
	void UpdateRenderInfo(
		VkExtent2D extent,
		const std::vector<AttachmentDesc>& attachments);

	void DrawIndexedIndirect(
		VkCommandBuffer cmd,
		VkBuffer indirectBuffer,
		const IndirectDrawRange& drawRange,
		const PipelineHandle& pipeHandle,
		PushDescriptorWriter& pushWriter);
	void DrawVertexPull(
		VkCommandBuffer cmd,
		VkBuffer vertexBuffer,
		const VkDeviceSize vertexOffset,
		const std::vector<uint32_t>& drawOffsets,
		const IndirectDrawRange& drawRange,
		const PipelineHandle& pipeHandle);

	// Only used for skybox draw
	void DrawTriangle(
		VkCommandBuffer cmd,
		const IndirectDrawRange& drawRange,
		const PipelineHandle& pipeHandle);

	void BeginRendering(VkCommandBuffer cmd);

	void EndRendering(VkCommandBuffer cmd);

private:
	void InitGraphics(
		VkExtent2D extent,
		const std::vector<AttachmentDesc>& images,
		bool isAtlas = false);

	void ClearAllAttachments()
	{
		m_colorAttachments.clear();
		m_depthAttachment = {};
		m_bHasDepth = false;
	}
	void WriteColorAttachmentInfo(const AttachmentDesc& desc)
	{
		m_colorAttachments.emplace_back(VkRenderingAttachmentInfo{
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = desc.imageView,
			.imageLayout = desc.layout,
			.resolveImageView = desc.resolveView,
			.resolveImageLayout = desc.resolveLayout,
			.loadOp = desc.loadOp,
			.storeOp = desc.storeOp,
			.clearValue = desc.clearValue
		});
	}
	void WriteDepthAttachmentInfo(const AttachmentDesc& desc) noexcept
	{
		if (desc.layout != VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL ||
			desc.layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
			desc.layout != VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL ||
			desc.layout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
		{
			m_bHasDepth = false;
			m_depthAttachment = {};
			return;
		}
		m_bHasDepth = true;

		VkRenderingAttachmentInfo depthAttach {
			.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView = desc.imageView,
			.imageLayout = desc.layout,
			.resolveImageView = desc.resolveView,
			.resolveImageLayout = desc.resolveLayout,
			.loadOp = desc.loadOp,
			.storeOp = desc.storeOp,
			.clearValue = desc.clearValue
		};
		m_depthAttachment = depthAttach;
	}

	VkRenderingInfo m_info{};
	std::vector<VkRenderingAttachmentInfo> m_colorAttachments;
	VkRenderingAttachmentInfo m_depthAttachment{};
	bool m_bHasDepth = false;
	bool m_bHasAtlas = false;
	VkOffset2D m_atlasOffset{};
	VkExtent2D m_atlasExtent{};
};


class ComputeScope final : public RenderScope
{
public:
	ComputeScope(
		RendererDefinitions::Renderer_Pass pass,
		VkExtent2D extent,
		VkExtent3D workgroupSize = { 16u, 16u, 1u })
	{
		this->InitBase(pass);
		InitCompute(extent, workgroupSize);
	}

	void UpdateExtent(VkExtent2D extent) { m_extent = extent; }
	void UpdateWorkgroups(VkExtent3D size) { m_workgroupSize = size; }

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

	void DispatchComputePass(
		VkCommandBuffer cmd,
		const PipelineHandle& pipeHandle,
		PushDescriptorWriter& pushWriter);

private:
	VkExtent2D m_extent{ 0u, 0u };
	VkExtent3D m_workgroupSize{ 16u, 16u, 1u };
	uint32_t m_groupCountX = 0u;
	uint32_t m_groupCountY = 0u;
	uint32_t m_groupCountZ = 1u;

	void InitCompute(VkExtent2D extent, VkExtent3D workgroupSize)
	{
		m_extent = extent;
		m_workgroupSize = workgroupSize;
	}

	struct DispatchIndirectInfo
	{
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceSize offset = 0;
	};
	DispatchIndirectInfo m_indirect{};

	bool m_bSkipGroups = false;

	// Called right before vkDispatchCmd
	void CalculateGroups() noexcept
	{
		if (m_bSkipGroups) return;
		m_groupCountX = (m_extent.width + m_workgroupSize.width - 1u) / m_workgroupSize.width;
		m_groupCountY = (m_extent.height + m_workgroupSize.height - 1u) / m_workgroupSize.height;
		m_groupCountZ = m_workgroupSize.depth; // usually 1
	}

	bool IsIndirect() const noexcept { return m_indirect.buffer != VK_NULL_HANDLE; }
};
