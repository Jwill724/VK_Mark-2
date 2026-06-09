#pragma once

#include "renderer/RendererDefinitions.h"
namespace RD = RendererDefinitions;

#include "../backend/VulkanTypes.h"
#include "../backend/DescriptorWriter.h"

#include <variant>
#include <functional>
#include "scopes/ComputeScope.h"
#include "scopes/GraphicsScope.h"
#include "RenderGraphResources.h"

using ScopeVariant = std::variant<GraphicsScope, ComputeScope>;

struct RenderPassDesc
{
	std::string passName{};

	std::vector<PipelineHandle> pipelines;

	ScopeVariant scope;

	std::vector<RenderResourceUsage> resources;

	PushDescriptorWriter pushWriter;

	bool bAllowPassCulling = true;
	bool bForceExecution = false;

	std::function<bool(
		const RenderPassExecutionContext&)>
	shouldExecute;

	// setup stage before execution.
	std::function<void(
		RenderPassExecutionContext&,
		RenderPassDesc&)>
	setup;

	// Main record
	std::function<void(
		RenderPassExecutionContext&,
		RenderPassDesc&)>
	record;
};

class RenderPassBuilder
{
public:
	RenderPassBuilder(RenderPassDesc& desc) : m_desc(desc) {}

	// Direct first inputs of a possible pass/subpasses
	// TODO: Figure out how to make this read resource practical, not in use currently.
	RenderPassBuilder& ReadResource(
		RD::Renderer_RenderTarget target,
		RD::ImageAccess access,
		uint32_t baseMip = 0,
		uint32_t mipCount = 1);

	// Only set for a clear "goal" write, has to enter to write ONCE and transition to read at end of pass.
	RenderPassBuilder& WriteResource(
		RD::Renderer_RenderTarget target,
			RD::ImageAccess enterAccess,
			RD::ImageAccess exitAccess,
			uint32_t baseMip = 0,
			uint32_t mipCount = 1);

	RenderPassBuilder& SetSetup(
		std::function<void(
			RenderPassExecutionContext&,
			RenderPassDesc&)> fn)
	{
		m_desc.setup = std::move(fn);
		return *this;
	}

	RenderPassBuilder& SetRecord(
		std::function<void(
			RenderPassExecutionContext&,
			RenderPassDesc&)> fn)
	{
		m_desc.record = std::move(fn);
		return *this;
	}

	RenderPassBuilder& SetExecutionCondition(
		std::function<bool(
			const RenderPassExecutionContext&)> fn)
	{
		m_desc.shouldExecute = std::move(fn);
		return *this;
	}

	RenderPassBuilder& ForceExecution()
	{
		m_desc.bForceExecution = true;
		return *this;
	}

	RenderPassBuilder& DisableCulling()
	{
		m_desc.bAllowPassCulling = false;
		return *this;
	}

	// TODO: Function to add possible compute async work
	// like compute fill buffers earlier in frame
	// RenderPassBuilder& MarkAsyncWork()

private:
	RenderPassDesc& m_desc;
};
