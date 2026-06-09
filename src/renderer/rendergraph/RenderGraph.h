#pragma once

#include "RenderGraphBuilder.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

class BindlessImageTable;
struct RenderPassExecutionContext;
struct PipelineHandle;
class PipelineManager;

class GraphicsScope;
class ComputeScope;

class RenderGraph final
{
	friend class Renderer;
public:
	template<typename BuildFn>
	RenderPassDesc& AddPass(
		std::string name,
		std::vector<PipelineHandle> pipelines,
		BuildFn&& buildFn)
	{
		RenderPassDesc& pass = CreatePass(
			std::move(name),
			std::move(pipelines));

		RenderPassBuilder builder(pass);

		buildFn(builder);

		return pass;
	}

	void Sync(const RD::RenderStateInfo& frameState);

	void ExecutePasses(RenderPassExecutionContext& ctx);

	void RebuildActiveList();

	void SetDrawExtent(Extents2D extent)
	{
		m_drawExtent = extent;
	}

	const Extents2D& GetDrawExtent() const noexcept { return m_drawExtent; }

	bool IsFirstGraphicsWrite(RD::Renderer_RenderTarget t) const
	{
		return m_writtenThisFrame.find(t) == m_writtenThisFrame.end();
	}

private:
	RenderPassDesc& CreatePass(
		std::string name,
		std::vector<PipelineHandle> pipelines);

	void Build(
		PipelineManager& pipeManager,
		Extents2D drawExtent);

	void Shutdown();

	void ExecuteGraphicsPass(
		RenderPassExecutionContext& ctx,
		RenderPassDesc& pass,
		GraphicsScope& scope);

	void ExecuteComputePass(
		RenderPassExecutionContext& ctx,
		RenderPassDesc& pass,
		ComputeScope& scope);

	void TransitionResources(
		RenderPassExecutionContext& ctx,
		RenderPassDesc& pass);

	void PostTransitionResources(
		RenderPassExecutionContext& ctx,
		RenderPassDesc& pass);
	
	std::unordered_map<RD::Renderer_RenderTarget, RD::ImageAccess> m_trackedLayouts;
	std::unordered_set<RD::Renderer_RenderTarget> m_writtenThisFrame;

	std::vector<RenderPassDesc> m_passes;

	std::vector<size_t> m_activePassIndices;

	RD::RenderStateInfo m_recentFrameState{};

	bool m_bGraphDirty = true;

	Extents2D m_drawExtent;
};
