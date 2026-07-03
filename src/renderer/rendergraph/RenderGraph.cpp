#include "pch.h"

#include "RenderGraph.h"
#include "RenderPasses.h"

#include "../backend/PipelineManager.h"
#include "../backend/PipelineBundles.h"
#include "../backend/memory/BindlessImageTable.h"
#include "scopes/ComputeScope.h"
#include "scopes/GraphicsScope.h"
#include "../backend/ImageUtils.h"
#include "../backend/VulkanTypes.h"

RenderPassDesc& RenderGraph::CreatePass(
	std::string name,
	std::vector<PipelineHandle> pipelines)
{
	RenderPassDesc desc{};

	desc.passName  = std::move(name);
	desc.pipelines = std::move(pipelines);

	m_passes.push_back(std::move(desc));

	m_bGraphDirty = true;

	return m_passes.back();
}

void RenderGraph::Build(
	PipelineManager& pipeManager,
	Extents2D drawExtent)
{
	m_passes.clear();

	SetDrawExtent(drawExtent);

	// --- Prepass & HiZ ---
	RegisterTemporalCopyPass(*this,        {});
	RegisterThePrepass(*this,              pipeManager.GetBundle<BasePrepassPipelineSlot>());
	RegisterHiZGenerationPass(*this,       pipeManager.GetBundle<HiZGenerationPipelineSlot>());

	// --- Shadow passes ---
	RegisterDirectionalCSMPass(*this,      pipeManager.GetBundle<DirectionalCSMPipelineSlot>());
	RegisterFlashlightShadowMapPass(*this, pipeManager.GetBundle<FlashlightShadowPipelineSlot>());
	RegisterContactShadowsPass(*this,      pipeManager.GetBundle<SSContactShadowPipelineSlot>());

	// --- Light culling ---
	RegisterLightCullingPass(*this,        pipeManager.GetBundle<VisibleLightCullPipelineSlot>());
	RegisterClusteredLightsPass(*this,     pipeManager.GetBundle<ClusteredLightsPipelineSlot>());

	// --- AO ---
	RegisterSSAOPass(*this,                pipeManager.GetBundle<SSAOPipelineSlot>());

	// --- Opaque + Skybox ---
	RegisterSkyboxPass(*this,              pipeManager.GetBundle<SkyboxPipelineSlot>());
	RegisterOpaqueForwardPass(*this,       pipeManager.GetBundle<OpaqueForwardPipelineSlot>());
	RegisterOBBLineDebugPass(*this,        pipeManager.GetBundle<ObbDebugPipelineSlot>());

	// --- Transparent ---
	RegisterTransparentForwardPass(*this,  pipeManager.GetBundle<TransparentForwardPipelineSlot>());
	RegisterTransparentResolvePass(*this,  pipeManager.GetBundle<TransparentResolvePipelineSlot>());

	// --- Volumetrics ---
	RegisterVolumetricLightPass(*this,     pipeManager.GetBundle<VolumetricLightingPipelineSlot>());

	// --- TAA ---
	RegisterTAAPass(*this,                 pipeManager.GetBundle<TAAPipelineSlot>());

	// --- Post process ---
	RegisterLuminanceExposurePass(*this,   pipeManager.GetBundle<ExposurePipelineSlot>());
	RegisterBloomPass(*this,               pipeManager.GetBundle<BloomPipelineSlot>());
	RegisterLensFlarePass(*this,           pipeManager.GetBundle<LensFlarePipelineSlot>());
	RegisterFinalCompositePass(*this,      pipeManager.GetBundle<FinalCompositePipelineSlot>());

	// --- Post AA ---
	RegisterCMAA2Pass(*this,               pipeManager.GetBundle<CMAA2PipelineSlot>());
	RegisterSMAAPass(*this,                pipeManager.GetBundle<SMAAPipelineSlot>());
	RegisterFXAAPass(*this,                pipeManager.GetBundle<FXAAPipelineSlot>());

	// --- Final ---
	RegisterChromaticAberrationPass(*this, pipeManager.GetBundle<ChromaticAberrationPipelineSlot>());
	RegisterSwapchainPresentPass(*this,    {});
	RegisterImguiDrawPass(*this,           {});

	m_bGraphDirty = false;
	RebuildActiveList();
}

void RenderGraph::Shutdown()
{
	m_passes.clear();
	m_activePassIndices.clear();
	m_trackedLayouts.clear();
	m_bGraphDirty = true;
}

void RenderGraph::Sync(const RD::RenderStateInfo& frameState)
{
	bool stateChanged =
		(m_recentFrameState.activeLightCount      != frameState.activeLightCount)      ||
		(m_recentFrameState.bCopyPostAAImage      != frameState.bCopyPostAAImage)      ||
		(m_recentFrameState.bHasVisibles          != frameState.bHasVisibles)          ||
		(m_recentFrameState.bIsOpaqueVisible      != frameState.bIsOpaqueVisible)      ||
		(m_recentFrameState.bIsTransparentVisible != frameState.bIsTransparentVisible) ||
		(m_recentFrameState.bFlashlightOn         != frameState.bFlashlightOn)         ||
		(m_recentFrameState.bTemporalValid        != frameState.bTemporalValid)        ||
		(m_recentFrameState.bShowImgui            != frameState.bShowImgui);

	m_recentFrameState.bStateChanged = stateChanged;

	if (stateChanged)
	{
		m_recentFrameState = frameState;
		RebuildActiveList();
	}
}

// Internal — rebuild which passes will run this frame
void RenderGraph::RebuildActiveList()
{
	m_activePassIndices.clear();

	for (size_t i = 0; i < m_passes.size(); i++)
	{
		const auto& pass = m_passes[i];

		if (pass.bForceExecution)
		{
			m_activePassIndices.push_back(i);
			continue;
		}

		if (!pass.bAllowPassCulling)
		{
			m_activePassIndices.push_back(i);
			continue;
		}

		// Deferred condition check — shouldExecute runs at execute time
		// but we can pre-cull statically known dead passes here
		m_activePassIndices.push_back(i);
	}
}

void RenderGraph::ExecutePasses(RenderPassExecutionContext& ctx)
{
	ASSERT(!m_bGraphDirty);

	// Reset tracked layouts each frame — all render targets start unknown
	m_trackedLayouts.clear();
	m_writtenThisFrame.clear();
	ctx.renderGraph = this;

	for (auto idx : m_activePassIndices)
	{
		auto& pass = m_passes[idx];
		if (pass.shouldExecute && !pass.shouldExecute(ctx)) continue;

		// Enter transitions (before setup so push binds see correct layout)
		TransitionResources(ctx, pass);

		if (pass.setup) pass.setup(ctx, pass);

		std::visit([&](auto& scope)
		{
			using T = std::decay_t<decltype(scope)>;
			if constexpr (std::is_same_v<T, GraphicsScope>)
				ExecuteGraphicsPass(ctx, pass, scope);
			else if constexpr (std::is_same_v<T, ComputeScope>)
				ExecuteComputePass(ctx, pass, scope);
		}, pass.scope);

		// Exit transitions (auto — after record)
		PostTransitionResources(ctx, pass);

		// Chaining graphics color attachments
		for (const auto& res : pass.resources)
			if (res.bIsWrite) m_writtenThisFrame.insert(res.target);
	}
}

void RenderGraph::TransitionResources(
	RenderPassExecutionContext& ctx,
	RenderPassDesc& pass)
{
	for (const auto& res : pass.resources)
	{
		const auto& image = ctx.imageTable->GetRenderTarget(res.target);

		auto it = m_trackedLayouts.find(res.target);
		RD::ImageAccess currentAccess = (it != m_trackedLayouts.end())
			? it->second
			: RD::ImageAccess::Undefined;

		if (currentAccess != res.enterAccess)
		{
			ImageUtils::TransitionLayout(
				ctx.commandBuffer,
				image,
				currentAccess,
				res.enterAccess,
				res.baseMip,
				res.mipCount);
		}

		m_trackedLayouts[res.target] = res.enterAccess;
	}
}

void RenderGraph::PostTransitionResources(
	RenderPassExecutionContext& ctx,
	RenderPassDesc& pass)
{
	for (const auto& res : pass.resources)
	{
		if (res.bManualExitTransition) continue;
		if (res.exitAccess == res.enterAccess) continue;

		const auto& image = ctx.imageTable->GetRenderTarget(res.target);

		ImageUtils::TransitionLayout(
			ctx.commandBuffer,
			image,
			res.enterAccess,
			res.exitAccess,
			res.baseMip,
			res.mipCount);

		m_trackedLayouts[res.target] = res.exitAccess;
	}
}

void RenderGraph::ExecuteComputePass(
	RenderPassExecutionContext& ctx,
	RenderPassDesc& pass,
	ComputeScope& scope)
{
	if (pass.record)
		pass.record(ctx, pass);
}

void RenderGraph::ExecuteGraphicsPass(
	RenderPassExecutionContext& ctx,
	RenderPassDesc& pass,
	GraphicsScope& scope)
{
	if (pass.record)
		pass.record(ctx, pass);
}
