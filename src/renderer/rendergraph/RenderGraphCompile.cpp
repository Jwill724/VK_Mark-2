#include "pch.h"

#include "RenderGraph.h"
#include "RenderGraphResources.h"

// =====================================================================
// Sync — the per-frame compile.
// =====================================================================
void RenderGraph::Sync(
	const RD::RenderStateInfo& frameState,
	const RenderPassExecutionContext& ctx)
{
	ASSERT(frameState.RenderMode() != static_cast<uint32_t>(RD::RenderingMode::UNDEFINED)
		&& "Missing rendering mode.");
	ASSERT(!m_bGraphDirty && "Build() must run before Sync()");

	const bool stateChanged =
		(m_recentFrameState.CopyPostAAImage()     != frameState.CopyPostAAImage())     ||
		(m_recentFrameState.DebugRenderFastPath() != frameState.DebugRenderFastPath()) ||
		(m_recentFrameState.FlashlightOn()        != frameState.FlashlightOn())        ||
		(m_recentFrameState.IsObbLineOn()         != frameState.IsObbLineOn())         ||
		(m_recentFrameState.IsTemporalValid()     != frameState.IsTemporalValid())     ||
		(m_recentFrameState.IsHiZValid()          != frameState.IsHiZValid())          ||
		(m_recentFrameState.DrawImgui()           != frameState.DrawImgui())           ||
		(m_recentFrameState.RenderMode()          != frameState.RenderMode())          ||
		(m_recentFrameState.InstancesActive()     != frameState.InstancesActive() && frameState.InstancesActive() == 0u) ||
		(m_recentFrameState.LightsActive()        != frameState.LightsActive()    && frameState.LightsActive()    == 0u);

	m_recentFrameState.m_bStateChanged = stateChanged;

	if (stateChanged)
		m_recentFrameState = frameState;

	// shouldExecute reads only frameState / frameCtx flags, all final by
	// this point, so the active set is exact.
	EvaluateActivePasses(ctx);

	if (stateChanged || m_activeMask != m_prevActiveMask || m_bBatchesDirty)
	{
		BuildBatches();
		m_prevActiveMask = m_activeMask;
		m_bBatchesDirty  = false;
	}

	// Every frame: simulate layouts from the persistent tracker and bake per-pass barrier lists.
	BakeBarriers();

	m_schedule.bValid = true;
}

// =====================================================================
void RenderGraph::EvaluateActivePasses(const RenderPassExecutionContext& ctx)
{
	ASSERT(m_passes.size() <= 64u && "Widen the active mask past 64 passes");

	m_activeMask = 0ull;

	for (size_t i = 0; i < m_passes.size(); i++)
	{
		const auto& pass = m_passes[i];

		const bool active =
			pass.bForceExecution ||
			!pass.shouldExecute  ||
			pass.shouldExecute(ctx);

		if (active)
			m_activeMask |= (1ull << i);
	}

	m_schedule.activeMask = m_activeMask;
}

// =====================================================================
// BuildBatches — fixed four-slot layout, no allocation past frame 1.
// =====================================================================
void RenderGraph::BuildBatches()
{
	for (auto& b : m_schedule.batches)
		b.Reset();

	m_schedule.asyncRecordList.clear();
	m_schedule.bUsesAsyncCompute = false;
	m_schedule.graphicsBatchCount = 0u;
	m_schedule.bValid = false;

	auto isActive = [&](size_t i) { return (m_activeMask & (1ull << i)) != 0ull; };

	// Async only pays for itself when there is a distinct queue to run on.
	const bool bAsyncPossible = m_bAsyncComputeEnabled && m_bHasDedicatedComputeQueue;

	bool anyAsync   = false;
	bool anyOverlap = false;

	if (bAsyncPossible)
	{
		for (size_t i = 0; i < m_passes.size(); i++)
		{
			if (!isActive(i)) continue;

			const auto& pass = m_passes[i];

			if (pass.bAsyncCompute)
				anyAsync = true;
			else if (pass.phase == RenderPhase::AsyncWindow)
				anyOverlap = true;
		}
	}

	anyAsync = anyAsync && anyOverlap;

	SubmitBatch& g0 = m_schedule.Get(BatchId::G0);
	SubmitBatch& c0 = m_schedule.Get(BatchId::C0);
	SubmitBatch& g1 = m_schedule.Get(BatchId::G1);
	SubmitBatch& g2 = m_schedule.Get(BatchId::G2);

	g0.queue = PassQueue::Graphics;
	c0.queue = PassQueue::AsyncCompute;
	g1.queue = PassQueue::Graphics;
	g2.queue = PassQueue::Graphics;

	auto push = [&](SubmitBatch& batch, size_t passIndex, PassQueue queue)
	{
		PassScheduleInfo info{};
		info.passIndex = static_cast<uint32_t>(passIndex);
		info.queue     = queue;
		batch.passes.push_back(std::move(info));
		batch.bActive = true;

		for (const auto& res : m_passes[passIndex].resources)
			batch.touchedTargets.set(static_cast<size_t>(res.target));
	};

	if (!anyAsync)
	{
		for (size_t i = 0; i < m_passes.size(); i++)
			if (isActive(i))
				push(g0, i, PassQueue::Graphics);

		m_schedule.graphicsBatchCount = g0.bActive ? 1u : 0u;
		return;
	}

	m_schedule.bUsesAsyncCompute = true;

	for (size_t i = 0; i < m_passes.size(); i++)
	{
		if (!isActive(i)) continue;

		const auto& pass = m_passes[i];

		if (pass.bAsyncCompute)
		{
			ASSERT(pass.phase == RenderPhase::AsyncWindow &&
				"Async pass not in the AsyncWindow phase");

			m_schedule.asyncRecordList.push_back(
				static_cast<uint32_t>(c0.passes.size()));

			push(c0, i, PassQueue::AsyncCompute);
		}
		else if (pass.phase <= RenderPhase::Prepass)
		{
			push(g0, i, PassQueue::Graphics);
		}
		else if (pass.phase == RenderPhase::AsyncWindow)
		{
			push(g1, i, PassQueue::Graphics);
		}
		else
		{
			push(g2, i, PassQueue::Graphics);
		}
	}

	ASSERT(g0.bActive && "Async batch needs a preceding graphics batch to wait on.");
	ASSERT(g1.bActive && "Async split with an empty G1 — the anyOverlap check should have fallen back to the single-batch path.");
	ASSERT(g2.bActive && "Async batch cannot be last — present lives in G2.");

	m_schedule.graphicsBatchCount =
		(g0.bActive ? 1u : 0u) + (g1.bActive ? 1u : 0u) + (g2.bActive ? 1u : 0u);

	ASSERT(m_schedule.graphicsBatchCount <= MAX_GRAPHICS_PRIMARIES);

	ValidateConcurrentBatches();
}

struct ImageUse
{
	RD::ImageAccess access;
	bool            bWrite;
	const char*     passName;
};

bool FindImageUse(
	const std::vector<RenderPassDesc>& passes,
	const SubmitBatch& batch,
	RD::Renderer_RenderTarget target,
	ImageUse& out)
{
	for (const auto& info : batch.passes)
	{
		const RenderPassDesc& pass = passes[info.passIndex];

		for (const auto& res : pass.resources)
		{
			if (res.target != target) continue;

			out.access    = res.enterAccess;
			out.bWrite    = res.bIsWrite || res.bManualExitTransition;
			out.passName  = pass.passName.c_str();
			return true;
		}
	}
	return false;
}

void RenderGraph::ValidateConcurrentBatches() const
{
	const SubmitBatch& c0 = m_schedule.Get(BatchId::C0);
	const SubmitBatch& g1 = m_schedule.Get(BatchId::G1);

	if (!c0.bActive || !g1.bActive) return;

	for (size_t t = 0; t < RD::RENDER_TARGET_COUNT; ++t)
	{
		const auto target = static_cast<RD::Renderer_RenderTarget>(t);

		ImageUse computeUse{};
		ImageUse graphicsUse{};

		if (!FindImageUse(m_passes, c0, target, computeUse))  continue;
		if (!FindImageUse(m_passes, g1, target, graphicsUse)) continue;

		const bool bEitherWrites =
			computeUse.bWrite || graphicsUse.bWrite;

		const bool bLayoutMismatch =
			computeUse.access != graphicsUse.access;

		if (!bEitherWrites && !bLayoutMismatch) continue;

		if (bEitherWrites)
		{
			ASSERT(false);
		}
		else
		{
			ASSERT(false);
		}
	}

}

// =====================================================================
// BakeBarriers
//
// Walks batches in array order G0, C0, G1, G2 and simulates layouts
// against the persistent tracker.
//
// Walking C0 before G1 is valid precisely BECAUSE they are disjoint
// (enforced above) — with no shared targets, the relative order of the
// two concurrent batches cannot change the result.
// =====================================================================
void RenderGraph::BakeBarriers()
{
	// Frame-local first-writer tracking.
	TargetSet writtenThisFrame;

	SubmitBatch* lastGraphicsBatch = nullptr;

	for (uint32_t b = 0; b < MAX_SUBMIT_BATCHES; ++b)
	{
		SubmitBatch& batch = m_schedule.batches[b];

		batch.tailBarriers.clear();

		if (!batch.bActive) continue;

		const bool bAsyncBatch = (batch.queue == PassQueue::AsyncCompute);

		// Targets already transitioned inside this batch. An async
		// batch's FIRST touch of a target is folded into the previous
		// graphics batch's tail (graphics-legal stage masks; the timeline
		// signal orders it and makes it visible). Subsequent transitions
		// stay on the compute cmd and use the compute-safe variant.
		TargetSet touchedThisBatch;

		for (auto& info : batch.passes)
		{
			info.enterBarriers.clear();
			info.exitBarriers.clear();
			info.firstWriteTargets.reset();
			info.recordedCmd = VK_NULL_HANDLE;

			const auto& pass = m_passes[info.passIndex];

			// --- enter transitions ---------------------------------
			for (const auto& res : pass.resources)
			{
				const size_t t = static_cast<size_t>(res.target);
				const RD::ImageAccess current = m_trackedLayouts[t];

				if (res.bIsWrite && !writtenThisFrame.test(t))
				{
					info.firstWriteTargets.set(t);
					writtenThisFrame.set(t);
				}

				if (current != res.enterAccess)
				{
					const BakedImageBarrier bar{
						res.target, current, res.enterAccess,
						res.baseMip, res.mipCount };

					if (bAsyncBatch && !touchedThisBatch.test(t))
					{
						ASSERT(lastGraphicsBatch != nullptr);

						lastGraphicsBatch->tailBarriers.push_back(bar);
					}
					else
					{
						info.enterBarriers.push_back(bar);
					}

					m_trackedLayouts[t] = res.enterAccess;
				}

				touchedThisBatch.set(t);
			}

			// --- exit transitions ----------------------------------
			for (const auto& res : pass.resources)
			{
				const size_t t = static_cast<size_t>(res.target);

				if (res.bManualExitTransition)
				{
					// InternalResource: the pass owns its internal
					// transitions and guarantees this final state. Track
					// it, emit nothing.
					m_trackedLayouts[t] = res.exitAccess;
					continue;
				}

				if (res.exitAccess == res.enterAccess) continue;

				// Async exits stay in-batch and replay through the
				// compute-safe transition, so G2 sees truthful tracked
				// state with no deferred bookkeeping.
				info.exitBarriers.emplace_back(BakedImageBarrier{
					res.target, res.enterAccess, res.exitAccess,
					res.baseMip, res.mipCount });

				m_trackedLayouts[t] = res.exitAccess;
			}
		}

		if (!bAsyncBatch)
			lastGraphicsBatch = &batch;
	}
}
