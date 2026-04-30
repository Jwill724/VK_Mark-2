#include "pch.h"

#include "ScopeFunctions.h"
#include <renderer/backend/DescriptorWriter.h>
#include "common/ResourceTypes.h"

static constexpr VkDeviceSize DRAW_CMD_SIZE = sizeof(VkDrawIndexedIndirectCommand);

static constexpr void DefineViewportAndScissor(VkCommandBuffer cmd, VkExtent2D drawExtent) noexcept
{
	VkViewport viewport {
		.x = 0.0f,
		.y = static_cast<float>(drawExtent.height),
		.width = static_cast<float>(drawExtent.width),
		.height = -static_cast<float>(drawExtent.height),
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

static constexpr void DefineViewportAndScissorAtlas(
	VkCommandBuffer cmd,
	VkOffset2D offset,
	VkExtent2D extent) noexcept
{
	VkViewport viewport{};
	viewport.x = static_cast<float>(offset.x);
	viewport.y = static_cast<float>(offset.y + static_cast<int32_t>(extent.height));
	viewport.width = static_cast<float>(extent.width);
	viewport.height = -static_cast<float>(extent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor{};
	scissor.offset = offset;
	scissor.extent = extent;

	vkCmdSetViewport(cmd, 0, 1, &viewport);
	vkCmdSetScissor(cmd, 0, 1, &scissor);
}

template<typename T>
void RenderScope::SetPush(T& data) noexcept
{
	static_assert(std::is_trivially_copyable_v<T>);
	m_pushData = &data;
	m_pushSize = sizeof(T);
}

void RenderScope::ClearPush() noexcept {
	m_pushData = nullptr;
	m_pushSize = 0u;
}

template <typename T, typename Fn>
void RenderScope::EditPush(Fn&& fn) noexcept
{
	ASSERT(m_pushData != nullptr);
	ASSERT(m_pushSize == sizeof(T));
	ASSERT((reinterpret_cast<uintptr_t>(m_pushData) % alignof(T)) == 0);

	T* push = static_cast<T*>(m_pushData);
	fn(*push);
}
void RenderScope::BindPushConstant(VkCommandBuffer cmd, const PipelineHandle& pipeHandle)
{
	if (m_pushData && m_pushSize > 0 && !m_bSkipPushConstant)
	{
		vkCmdPushConstants(
			cmd,
			pipeHandle.layout.pipelineLayout,
			pipeHandle.layout.pushConstantDef.stageFlags,
			pipeHandle.layout.pushConstantDef.offset,
			static_cast<uint32_t>(m_pushSize),
			m_pushData);
	}
}

void GraphicsScope::InitGraphics(
	VkExtent2D extent,
	const std::vector<AttachmentDesc>& images,
	bool isAtlas)
{
	for (const auto& desc : images)
	{
		WriteColorAttachmentInfo(desc);

		// Only one depth attachment can exist
		if (!m_bHasDepth)
		{
			WriteDepthAttachmentInfo(desc);
		}
	}

	if (isAtlas)
	{
		m_bHasAtlas = isAtlas;
		m_info.renderArea.offset = m_atlasOffset;
		m_info.renderArea.extent = m_atlasExtent;
	}

	m_info.colorAttachmentCount = static_cast<uint32_t>(m_colorAttachments.size());
	m_info.pColorAttachments = m_colorAttachments.data();
	m_info.pDepthAttachment = m_bHasDepth ? &m_depthAttachment : nullptr;

	m_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	m_info.renderArea.offset = { 0, 0 };
	m_info.renderArea.extent = extent;
	m_info.layerCount = 1;
}

void GraphicsScope::UpdateRenderInfo(
	VkExtent2D extent,
	const std::vector<AttachmentDesc>& images)
{
	ClearAllAttachments();

	for (const auto& desc : images)
	{
		WriteColorAttachmentInfo(desc);

		// Only one depth attachment can exist
		if (!m_bHasDepth)
		{
			WriteDepthAttachmentInfo(desc);
		}
	}
	m_info.colorAttachmentCount = static_cast<uint32_t>(m_colorAttachments.size());
	m_info.pColorAttachments = m_colorAttachments.data();
	m_info.pDepthAttachment = m_bHasDepth ? &m_depthAttachment : nullptr;
	m_info.renderArea.extent = extent;
}

void GraphicsScope::BeginRendering(VkCommandBuffer cmd)
{
	// Keep atlasing updated
	if (m_bHasAtlas)
	{
		m_info.renderArea.offset = m_atlasOffset;
		m_info.renderArea.extent = m_atlasExtent;

		vkCmdBeginRendering(cmd, &m_info);

		DefineViewportAndScissorAtlas(
			cmd,
			m_atlasOffset,
			m_atlasExtent);
		return;
	}

	vkCmdBeginRendering(cmd, &m_info);
	DefineViewportAndScissor(cmd, m_info.renderArea.extent);
}
void GraphicsScope::EndRendering(VkCommandBuffer cmd)
{
	vkCmdEndRendering(cmd);
}

void GraphicsScope::DrawIndexedIndirect(
	VkCommandBuffer cmd,
	VkBuffer indirectBuffer,
	const IndirectDrawRange& drawRange,
	const PipelineHandle& pipeHandle,
	PushDescriptorWriter& pushWriter)
{
	vkCmdBindPipeline(
		cmd,
		pipeHandle.bindPoint,
		pipeHandle.pipeline);

	BindPushConstant(cmd, pipeHandle);

	pushWriter.UpdatePushLayout(
		cmd,
		pipeHandle.bindPoint,
		pipeHandle.layout.pipelineLayout);

	vkCmdDrawIndexedIndirect(
		cmd,
		indirectBuffer,
		drawRange.firstCommand * DRAW_CMD_SIZE,
		drawRange.commandCount,
		DRAW_CMD_SIZE);
}

void GraphicsScope::DrawVertexPull(
	VkCommandBuffer cmd,
	VkBuffer vertexBuffer,
	const VkDeviceSize vertexOffset,
	const std::vector<uint32_t>& drawOffsets,
	const IndirectDrawRange& drawRange,
	const PipelineHandle& pipeHandle)
{
	vkCmdBindPipeline(
		cmd,
		pipeHandle.bindPoint,
		pipeHandle.pipeline);

	BindPushConstant(cmd, pipeHandle); // Address pull through push constant

	vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &vertexOffset);

	for (const auto& drawOffset : drawOffsets)
	{
		vkCmdDraw(cmd, RendererDefinitions::VERTS_LINE_COUNT, 1, drawOffset, 0);
	}
}
void GraphicsScope::DrawTriangle(
	VkCommandBuffer cmd,
	const IndirectDrawRange& drawRange,
	const PipelineHandle& pipeHandle)
{
	vkCmdBindPipeline(
		cmd,
		pipeHandle.bindPoint,
		pipeHandle.pipeline);

	BindPushConstant(cmd, pipeHandle);

	vkCmdDraw(cmd, 3, 1, 0, 0);
}

void ComputeScope::DispatchComputePass(
	VkCommandBuffer cmd,
	const PipelineHandle& pipeHandle,
	PushDescriptorWriter& pushWriter)
{
	vkCmdBindPipeline(cmd, pipeHandle.bindPoint, pipeHandle.pipeline);

	BindPushConstant(cmd, pipeHandle);

	pushWriter.UpdatePushLayout(
		cmd,
		pipeHandle.bindPoint,
		pipeHandle.layout.pipelineLayout);

	if (IsIndirect())
	{
		vkCmdDispatchIndirect(
			cmd,
			m_indirect.buffer,
			m_indirect.offset);
		return;
	}

	CalculateGroups();

	vkCmdDispatch(
		cmd,
		m_groupCountX,
		m_groupCountY,
		m_groupCountZ);
}
