#include "pch.h"

#include "GraphicsScope.h"
#include "../../backend/descriptors/DescriptorWriter.h"
#include "EngineTypes.h"

static void DefineViewportAndScissor(VkCommandBuffer cmd, VkExtent2D drawExtent) noexcept
{
	ASSERT(drawExtent.width > 0);
	ASSERT(drawExtent.height > 0);

	VkViewport viewport {
		.x = 0.0f,
		.y = static_cast<float>(drawExtent.height),
		.width = static_cast<float>(drawExtent.width),
		.height = -static_cast<float>(drawExtent.height), // Flipped y
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	};
	vkCmdSetViewport(cmd, 0, 1, &viewport);

	VkRect2D scissor {
		.offset{ 0, 0 },
		.extent{ drawExtent.width, drawExtent.height}
	};
	vkCmdSetScissor(cmd, 0, 1, &scissor);
}

static void DefineViewportAndScissorAtlas(
	VkCommandBuffer cmd,
	VkOffset2D offset,
	VkExtent2D extent) noexcept
{
	VkViewport viewport{};
	viewport.x = static_cast<float>(offset.x);
	viewport.y = static_cast<float>(offset.y + static_cast<int32_t>(extent.height));
	viewport.width = static_cast<float>(extent.width);
	viewport.height = -static_cast<float>(extent.height); // Flipped y
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor{};
	scissor.offset = offset;
	scissor.extent = extent;

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);
}

void GraphicsScope::ApplyViewport(VkCommandBuffer cmd)
{
	ASSERT(m_renderArea.extent.width > 0);
	ASSERT(m_renderArea.extent.height > 0);

	if (m_bHasAtlas)
	{
		DefineViewportAndScissorAtlas(
			cmd,
			m_renderArea.offset,
			m_renderArea.extent);
	}
	else
	{
		DefineViewportAndScissor(
			cmd,
			m_renderArea.extent);
	}
}

void GraphicsScope::UpdateRenderInfo(
	Extents2D extent,
	const std::vector<AttachmentDesc>& images,
	bool isAtlas)
{
	ClearAllAttachments();

	for (const auto& desc : images)
	{
		WriteColorAttachmentInfo(desc);

		// Only one depth attachment can exist
		WriteDepthAttachmentInfo(desc);
	}

	m_bHasAtlas = isAtlas;

	m_renderArea.offset = { 0, 0 };
	m_renderArea.extent =
	{
		extent.Width(),
		extent.Height()
	};

	if (isAtlas)
	{
		m_atlasOffset = { 0, 0 };

		m_atlasExtent =
		{
			extent.Width(),
			extent.Height()
		};

		m_renderArea.offset = m_atlasOffset;
		m_renderArea.extent = m_atlasExtent;
	}

	bool hasColor = m_colorAttachments.size() > 0;
	bool hasDepth = m_depthAttachment.imageView != VK_NULL_HANDLE;

	m_info.colorAttachmentCount = static_cast<uint32_t>(m_colorAttachments.size());
	m_info.pColorAttachments =  hasColor ? m_colorAttachments.data() : nullptr;
	m_info.pDepthAttachment = hasDepth ? &m_depthAttachment : nullptr;

	m_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	m_info.layerCount = 1;
	m_info.renderArea = m_renderArea;

	ASSERT(m_renderArea.extent.width > 0);
	ASSERT(m_renderArea.extent.height > 0);
}

void GraphicsScope::BeginRendering(VkCommandBuffer cmd)
{
	m_info.renderArea = m_renderArea;
	vkCmdBeginRendering(cmd, &m_info);

	// Atlas viewport more manual
	if (m_bHasAtlas) return;

	ApplyViewport(cmd);
}

void GraphicsScope::EndRendering(VkCommandBuffer cmd)
{
	vkCmdEndRendering(cmd);
}

// ---------------
// Draw functions
// ---------------

void GraphicsScope::DrawMeshTasksIndirectCount(
	VkCommandBuffer cmd,
	uint32_t slot,
	VkBuffer taskDispatchBuffer,
	VkBuffer countBuffer,
	const PipelineHandle& pipeline,
	PushDescriptorWriter& writer)
{
	vkCmdBindPipeline(
		cmd,
		pipeline.bindPoint,
		pipeline.pipeline);

	BindPushConstant(cmd, pipeline);

	writer.UpdatePushLayout(
		cmd,
		pipeline.bindPoint,
		pipeline.layout.pipelineLayout);

	vkCmdDrawMeshTasksIndirectCountEXT(
		cmd,
		taskDispatchBuffer,
		RD::TASK_BYTE_OFFSET_BY_SLOT[slot],
		countBuffer,
		static_cast<uint64_t>(slot * 4u),
		RD::MAX_TASK_DISPATCHES_BY_SLOT[slot],
		RD::TASK_GROUP_SIZE);
}

void GraphicsScope::DrawIndirect(
	VkCommandBuffer cmd,
	VkBuffer indirectDrawBuffer,
	VkBuffer vertexBuffer,
	const VkDeviceSize indirectOffset,
	const VkDeviceSize vertexOffset,
	const PipelineHandle& pipeHandle)
{
	vkCmdBindPipeline(
		cmd,
		pipeHandle.bindPoint,
		pipeHandle.pipeline);

	BindPushConstant(cmd, pipeHandle);

	vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &vertexOffset);

	vkCmdDrawIndirect(cmd, indirectDrawBuffer, indirectOffset, 1, RD::INDIRECT_CMD_SIZE);
}

void GraphicsScope::DrawTriangle(
	VkCommandBuffer cmd,
	const PipelineHandle& pipeHandle)
{
	vkCmdBindPipeline(
		cmd,
		pipeHandle.bindPoint,
		pipeHandle.pipeline);

	BindPushConstant(cmd, pipeHandle);

	vkCmdDraw(cmd, 3, 1, 0, 0);
}
