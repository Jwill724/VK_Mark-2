#include "pch.h"

#include "RenderGraph.h"
#include "RenderPasses.h"
#include "../backend/PipelineBundles.h"
#include "../backend/PipelineManager.h"
#include "RenderGraphSchedule.h"
#include "RenderGraphResources.h"
#include "../frame/FrameContext.h"
#include "../backend/ImageUtils.h"
#include "../backend/memory/BindlessImageTable.h"
#include "../../core/JobSystem.h"
#include "../../common/EngineTypes.h"

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
	Extents2D drawExtent,
	bool bHasDedicatedComputeQueue)
{
	m_passes.clear();

	SetDrawExtent(drawExtent);

	m_bHasDedicatedComputeQueue = bHasDedicatedComputeQueue;

	// --- Visibility ---
	RegisterTemporalCopyPass(*this,        {});
	RegisterShadowBoundsPass(*this,        pipeManager.GetBundle<ShadowBoundsPipelineSlot>());
	RegisterInstanceCullPass(*this,        pipeManager.GetBundle<InstanceCullPipelineSlot>());
	RegisterDrawBuildPass(*this,           pipeManager.GetBundle<DrawBuildPipelineSlot>());

	// --- Prepass & HiZ ---
	RegisterThePrepass(*this,              pipeManager.GetBundle<BasePrepassPipelineSlot>());
	RegisterHiZGenerationPass(*this,       pipeManager.GetBundle<HiZGenerationPipelineSlot>());

	RegisterWireframePass(*this,           pipeManager.GetBundle<OpaqueWireframePipelineSlot>());

	// --- Shadow raster ---
	RegisterDirectionalCSMPass(*this,      pipeManager.GetBundle<DirectionalCSMPipelineSlot>());
	RegisterFlashlightShadowMapPass(*this, pipeManager.GetBundle<FlashlightShadowPipelineSlot>());

	// --- Material resolve ---
	RegisterMaterialResolvePass(*this,     pipeManager.GetBundle<MaterialResolvePipelineSlot>());

	// --- LightingAO ---
	RegisterSSAOPass(*this,                pipeManager.GetBundle<SSAOPipelineSlot>());
	RegisterClusteredLightsPass(*this,     pipeManager.GetBundle<ClusteredLightsPipelineSlot>());
	RegisterContactShadowsPass(*this,      pipeManager.GetBundle<SSContactShadowPipelineSlot>());

	// --- Opaque + Skybox ---
	RegisterSkyboxPass(*this,              pipeManager.GetBundle<SkyboxPipelineSlot>());
	// Either one of these occurs
	RegisterOpaqueForwardPass(*this,       pipeManager.GetBundle<OpaqueForwardPipelineSlot>());
	RegisterOpaqueTileShadingPass(*this,   pipeManager.GetBundle<OpaqueTileShadingPipelineSlot>());

	// --- Transparent ---
	RegisterTransparentForwardPass(*this,  pipeManager.GetBundle<TransparentForwardPipelineSlot>());
	RegisterTransparentResolvePass(*this,  pipeManager.GetBundle<TransparentResolvePipelineSlot>());

	// --- Volumetrics ---
	RegisterVolumetricLightPass(*this,     pipeManager.GetBundle<VolumetricLightingPipelineSlot>());

	// --- Debug Line ---
	RegisterDebugDrawBuildPass(*this,      pipeManager.GetBundle<DebugBuildPipelineSlot>());
	RegisterLineDebugPass(*this,           pipeManager.GetBundle<LineDebugPipelineSlot>());

	// --- TAA ---
	RegisterTAAPass(*this,                 pipeManager.GetBundle<TAAPipelineSlot>());

	// --- Post process ---
	RegisterLuminanceExposurePass(*this,   pipeManager.GetBundle<ExposurePipelineSlot>());
	RegisterBloomPass(*this,               pipeManager.GetBundle<BloomPipelineSlot>());
	RegisterLensFlarePass(*this,           pipeManager.GetBundle<LensFlarePipelineSlot>());
	RegisterFinalCompositePass(*this,      pipeManager.GetBundle<FinalCompositePipelineSlot>());

	// --- Debug gbuffer deferred ---
	RegisterGBufferDebugPass(*this,        pipeManager.GetBundle<GBufferDebugPipelineSlot>());

	// --- Post AA ---
	RegisterCMAA2Pass(*this,               pipeManager.GetBundle<CMAA2PipelineSlot>());
	RegisterSMAAPass(*this,                pipeManager.GetBundle<SMAAPipelineSlot>());
	RegisterFXAAPass(*this,                pipeManager.GetBundle<FXAAPipelineSlot>());
	RegisterChromaticAberrationPass(*this, pipeManager.GetBundle<ChromaticAberrationPipelineSlot>());

	// --- Final ---
	RegisterSwapchainPresentPass(*this,    {});
	RegisterImguiDrawPass(*this,           {});

	m_bGraphDirty = false;
}

void RenderGraph::Shutdown()
{
	m_passes.clear();
	m_bGraphDirty = true;
	*this = RenderGraph{};
}

void RenderGraph::FlushBakedBarriers(
	VkCommandBuffer cmd,
	PassQueue queue,
	const std::vector<BakedImageBarrier>& barriers,
	BindlessImageTable& imageTable) const
{
	for (const BakedImageBarrier& b : barriers)
	{

		const AllocatedImage& img = imageTable.GetRenderTarget(b.target);

		if (queue == PassQueue::AsyncCompute)
		{
			// Filters stage/access down to the compute-legal subset:
			// GetImageSyncScope returns fragment-stage and attachment
			// bits that are illegal to submit on a compute queue.
			ImageUtils::TransitionLayoutCompute(
				cmd, img, b.oldAccess, b.newAccess, b.baseMip, b.mipCount);
		}
		else
		{
			ImageUtils::TransitionLayout(
				cmd, img, b.oldAccess, b.newAccess, b.baseMip, b.mipCount);
		}
	}
}

namespace
{
	void BeginSecondary(VkCommandBuffer cmd)
	{
		VkCommandBufferInheritanceInfo inherit{};
		inherit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;

		VkCommandBufferBeginInfo begin{};
		begin.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags            = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		begin.pInheritanceInfo = &inherit;

		VK_CHECK(vkBeginCommandBuffer(cmd, &begin));
	}

	void BeginPrimary(VkCommandBuffer cmd)
	{
		VkCommandBufferBeginInfo begin{};
		begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

		VK_CHECK(vkBeginCommandBuffer(cmd, &begin));
	}
}

void RenderGraph::RecordFrame(
	RenderPassExecutionContext& baseCtx,
	JobSystem& jobSystem,
	FrameContext& frameCtx,
	const RecordHooks& hooks)
{
	ASSERT(m_schedule.bValid && "Sync() must run before RecordFrame()");
	ASSERT(baseCtx.imageTable != nullptr);

	BindlessImageTable& imageTable = *baseCtx.imageTable;

	if (m_schedule.bUsesAsyncCompute)
		frameCtx.GetSecondaryArena().BeginFrame();

	RecordAsyncSecondaries(baseCtx, jobSystem, frameCtx, hooks);

	// Which graphics batch is last decides where onFrameEnd goes.
	uint32_t lastGraphicsBatchSlot = UINT32_MAX;
	for (uint32_t b = 0; b < MAX_SUBMIT_BATCHES; ++b)
	{
		const SubmitBatch& batch = m_schedule.batches[b];
		if (batch.bActive && batch.queue == PassQueue::Graphics)
			lastGraphicsBatchSlot = b;
	}
	ASSERT(lastGraphicsBatchSlot != UINT32_MAX && "Frame has no graphics work.");

	uint32_t graphicsPrimaryIdx = 0u;

	for (uint32_t b = 0; b < MAX_SUBMIT_BATCHES; ++b)
	{
		SubmitBatch& batch = m_schedule.batches[b];
		if (!batch.bActive) continue;

		if (batch.queue == PassQueue::AsyncCompute)
		{
			AssembleComputeBatch(
				batch,
				frameCtx.GetAsyncComputePrimary(),
				imageTable,
				hooks);
		}
		else
		{
			AssembleGraphicsBatch(
				batch,
				frameCtx.GetGraphicsPrimary(graphicsPrimaryIdx),
				baseCtx,
				hooks,
				graphicsPrimaryIdx == 0u,
				b == lastGraphicsBatchSlot);

			++graphicsPrimaryIdx;
		}
	}

	ASSERT(graphicsPrimaryIdx == m_schedule.graphicsBatchCount);
}

void RenderGraph::RecordAsyncSecondaries(
	RenderPassExecutionContext& baseCtx,
	JobSystem& jobSystem,
	FrameContext& frameCtx,
	const RecordHooks& hooks)
{
	if (m_schedule.asyncRecordList.empty()) return;

	SubmitBatch& c0 = m_schedule.Get(BatchId::C0);
	SecondaryCmdArena& arena = frameCtx.GetSecondaryArena();

	auto recordJob = [&](ThreadContext& threadCtx, uint32_t jobIndex)
	{
		const uint32_t slot = m_schedule.asyncRecordList[jobIndex];

		PassScheduleInfo& info = c0.passes[slot];
		RenderPassDesc&   pass = m_passes[info.passIndex];

		VkCommandBuffer cmd = arena.Acquire(threadCtx.threadID);

		BeginSecondary(cmd);

		if (hooks.bindPrologue)
			hooks.bindPrologue(cmd, PassQueue::AsyncCompute);

		RenderPassExecutionContext ctx = baseCtx;
		ctx.commandBuffer = cmd;
		ctx.scheduleInfo  = &info;
		ctx.threadSlot    = threadCtx.threadID;

		pass.record(ctx, pass);

		VK_CHECK(vkEndCommandBuffer(cmd));

		info.recordedCmd = cmd;
	};

	jobSystem.RunParallel(
		static_cast<uint32_t>(m_schedule.asyncRecordList.size()),
		recordJob);
}

void RenderGraph::AssembleGraphicsBatch(
	SubmitBatch& batch,
	VkCommandBuffer primary,
	RenderPassExecutionContext& baseCtx,
	const RecordHooks& hooks,
	bool bFirstGraphicsBatch,
	bool bLastGraphicsBatch)
{
	BindlessImageTable& imageTable = *baseCtx.imageTable;

	VK_CHECK(vkResetCommandBuffer(primary, 0));
	BeginPrimary(primary);

	if (bFirstGraphicsBatch && hooks.onFrameBegin)
		hooks.onFrameBegin(primary);

	if (hooks.bindPrologue)
		hooks.bindPrologue(primary, PassQueue::Graphics);

	for (auto& info : batch.passes)
	{
		RenderPassDesc& pass = m_passes[info.passIndex];

		FlushBakedBarriers(
			primary, PassQueue::Graphics, info.enterBarriers, imageTable);

		RenderPassExecutionContext ctx = baseCtx;
		ctx.commandBuffer = primary;
		ctx.scheduleInfo  = &info;
		ctx.threadSlot    = JobSystem::RENDER_THREAD;

		pass.record(ctx, pass);

		FlushBakedBarriers(
			primary, PassQueue::Graphics, info.exitBarriers, imageTable);
	}

	// Handoff transitions for the async batch waiting on this submit.
	// Emitted here, on the graphics queue, where the stage masks are
	// legal — the timeline signal makes them visible to the compute wait.
	FlushBakedBarriers(
		primary, PassQueue::Graphics, batch.tailBarriers, imageTable);

	if (bLastGraphicsBatch && hooks.onFrameEnd)
		hooks.onFrameEnd(primary);

	VK_CHECK(vkEndCommandBuffer(primary));
}

void RenderGraph::AssembleComputeBatch(
	SubmitBatch& batch,
	VkCommandBuffer primary,
	BindlessImageTable& imageTable,
	const RecordHooks& hooks)
{
	VK_CHECK(vkResetCommandBuffer(primary, 0));
	BeginPrimary(primary);

	for (auto& info : batch.passes)
	{
		ASSERT(info.recordedCmd != VK_NULL_HANDLE);

		FlushBakedBarriers(
			primary, PassQueue::AsyncCompute, info.enterBarriers, imageTable);

		vkCmdExecuteCommands(primary, 1u, &info.recordedCmd);

		FlushBakedBarriers(
			primary, PassQueue::AsyncCompute, info.exitBarriers, imageTable);

		info.recordedCmd = VK_NULL_HANDLE;
	}

	ASSERT(batch.tailBarriers.empty());

	if (hooks.onAsyncBatchEnd)
		hooks.onAsyncBatchEnd(primary);

	VK_CHECK(vkEndCommandBuffer(primary));
}
