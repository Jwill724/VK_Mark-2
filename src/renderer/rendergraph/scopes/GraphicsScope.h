#pragma once

#include "../RenderGraphResources.h"
#include "../../backend/VulkanTypes.h"

class PushDescriptorWriter;
struct IndirectDrawRange;

struct Extents2D;

class GraphicsScope final : public RenderScope
{
public:
	void UpdateAtlas(VkOffset2D offset, VkExtent2D extent) noexcept
	{
		m_atlasOffset = offset;
		m_atlasExtent = extent;
	}

	void ApplyViewport(VkCommandBuffer cmd);

	void UpdateRenderInfo(
		Extents2D extent,
		const std::vector<AttachmentDesc>& attachments,
		bool isAtlas = false);

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
		const PipelineHandle& pipeHandle);

	// Only used for skybox draw
	void DrawTriangle(
		VkCommandBuffer cmd,
		const PipelineHandle& pipeHandle);

	void BeginRendering(VkCommandBuffer cmd);

	void EndRendering(VkCommandBuffer cmd);

	VkExtent2D GetAtlasExtent() const { return m_atlasExtent; }
	VkOffset2D GetAtlasOffset() const noexcept { return m_atlasOffset; }
private:
	void ClearAllAttachments()
	{
		m_colorAttachments.clear();
		m_depthAttachment = {};
		m_bHasDepth = false;
	}
	void WriteColorAttachmentInfo(const AttachmentDesc& desc)
	{
		if (desc.imageLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) return;

		m_colorAttachments.emplace_back(VkRenderingAttachmentInfo{
			.sType              = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView          = desc.imageView,
			.imageLayout        = desc.imageLayout,
			.resolveImageView   = desc.resolveView,
			.resolveImageLayout = desc.resolveLayout,
			.loadOp             = desc.loadOp,
			.storeOp            = desc.storeOp,
			.clearValue         = desc.clearValue
		});
	}
	void WriteDepthAttachmentInfo(const AttachmentDesc& desc) noexcept
	{
		bool isDepth =
			desc.imageLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL ||
			desc.imageLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
			desc.imageLayout == VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL ||
			desc.imageLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

		if (!isDepth) return;

		m_bHasDepth = true;

		VkRenderingAttachmentInfo depthAttach {
			.sType               = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
			.imageView           = desc.imageView,
			.imageLayout         = desc.imageLayout,
			.resolveImageView    = desc.resolveView,
			.resolveImageLayout  = desc.resolveLayout,
			.loadOp              = desc.loadOp,
			.storeOp             = desc.storeOp,
			.clearValue          = desc.clearValue
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
