#pragma once

#include "renderer/frame/FrameContext.h"
#include "renderer/gpu/PipelineManager.h"
#include "engine/platform/profiler/Profiler.h"

namespace RenderPasses {
	void BasePrepass(
		FrameContext& frameCtx,
		GraphicsScope scope,
		Profiler& profiler,
		const bool isTemporalValid);
	void DirectionalCSMPass(
		FrameContext& frameCtx,
		GraphicsScope scope,
		Profiler& profiler);
	void ShadowFlashlightPass(
		FrameContext& frameCtx,
		GraphicsScope scope,
		Profiler& profiler);
	void SSAOPass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler,
		const bool isTemporalValid);
	void HiZGenerationPass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler);
	void VolumetricLightingPass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler);
	void SSContactShadowsPass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler);
	void ExposurePass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler,
		const AllocatedBuffer& luminanceBuf,
		const bool transparentVisible,
		const bool hasVisibles,
		const bool isTemporalValid);
	void LensFlarePass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler,
		const bool transparentVisible,
		const bool hasVisibles,
		const bool isTemporalValid);
	void FinalCompositePass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler,
		const bool transparentVisible,
		const bool hasVisibles,
		const bool isTemporalValid);
	void ChromaticAberrationPass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler,
		const bool hasVisibles);
	void ClusterLightCullingPass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler);
	void SMAAPass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler);
	void CMAA2Pass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler);
	void FXAAPass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler);
	void TAAPass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler);

	void TransparentResolvePass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler);

	void OpaqueForwardPass(
		FrameContext& frameCtx,
		GraphicsScope scope,
		Profiler& profiler);
	void TransparentForwardPass(
		FrameContext& frameCtx,
		GraphicsScope scope,
		Profiler& profiler);
	void SkyboxPass(
		FrameContext& frameCtx,
		GraphicsScope scope,
		Profiler& profiler,
		const bool hasVisibles);
	void ObbLineDebugPass(
		FrameContext& frameCtx,
		GraphicsScope scope,
		Profiler& profiler);
}
