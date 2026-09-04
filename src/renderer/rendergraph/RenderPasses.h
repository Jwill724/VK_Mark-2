#pragma once

#include <span>
#include <vector>
#include "../backend/VulkanForward.h"

// Pipelines are placed in order of execution
// Draw extent is equal to window extent

class RenderGraph;
struct PipelineHandle;
class BindlessImageTable;
class PushDescriptorWriter;

void RegisterDirectionalCSMPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterShadowBoundsPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterMaterialResolvePass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterOpaqueLightingPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterRTShadowsPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterNRDDenoisePass(RenderGraph& graph);

void RegisterInstanceCullPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterDrawBuildPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterTLASBuildPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterRTReflectionsPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterWireframePass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterChromaticAberrationPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterFlashlightShadowMapPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterVelocityResolvePass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterTemporalCopyPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterThePrepass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterHiZGenerationPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

// ==========================
// mesh shading additions

void RegisterThePrepassLate(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterHiZGenerationLatePass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);
// ==========================

void RegisterClusteredLightsPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

//void RegisterVolumetricFogPass(
//	RenderGraph& graph,
//	const std::vector<PipelineHandle> pipelines);

void RegisterVolumetricShadowMapPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterCMAA2Pass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterFXAAPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterSMAAPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterTAAPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterCASPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterDebugDrawBuildPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterLineDebugPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterLensFlarePass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterBloomPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterFinalCompositePass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterGBufferDebugPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterLuminanceExposurePass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterSSGIPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterContactShadowsPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterVolumetricLightPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterHDRSceneCompositePass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterImguiDrawPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterTransparentForwardPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterSkyboxPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void RegisterSwapchainPresentPass(
	RenderGraph& graph,
	const std::vector<PipelineHandle> pipelines);

void BakeEnvironmentMaps(
	VkCommandBuffer cmd,
	BindlessImageTable& imageTable,
	std::span<const PipelineHandle> pipelines);
