#pragma once

#include "backend/memory/ResourceAllocator.h"
#include "renderer/backend/DescriptorWriter.h"
#include "backend/memory/BindlessBDATable.h"
#include "backend/memory/BindlessImageTable.h"
#include "backend/Swapchain.h"
#include "frame/FrameContext.h"
#include "Material.h"
#include "Mesh.h"
#include "RendererDefinitions.h"
#include "profiler/Profiler.h"
#include "rendergraph/RenderGraph.h"
#include "rendergraph/RenderGraphResources.h"

namespace RD = RendererDefinitions;

class PipelineManager;
class DescriptorManager;
class Device;
class Window;
class JobSystem;
struct FrameStats;
class Scene;
struct GLFWwindow;
struct SceneUploadBatch;
struct ModelAsset;

inline constexpr bool LensFlareOn           = true;
inline constexpr bool ChromaticAberrationOn = true;
inline constexpr bool VolumetricsOn         = true;
inline constexpr bool ShadowsOn             = true;
inline constexpr bool ScreenSpaceShadowsOn  = true;
inline constexpr bool ProfilerViewOn        = false;
inline constexpr bool SettingsTabOn         = true;

// Manages core vulkan state and memory allocation.
// All primary resources are stored here, gpu buffers, image, frame data.
// Bindless textures, push descriptors for render targets, indirect buffer table for bindless gpu buffers.
class Renderer
{
public:
	void Init(
		const Window& window,
		JobSystem& jobSystem);
	void Cleanup();

	// Calls idle for device, should be only used at shutdown
	void StallDevice();

	bool ShouldRenderImgui() const noexcept
	{
		const auto& debug = m_profiler.debugToggles;
		return debug.enableProfilerView || debug.enableSettings;
	}

	void UploadScenes(std::vector<SceneUploadBatch>&& batches);
	void UnloadAllScenes();

	void RecordRenderCommand();

	bool PrepareFrame(); // Returns false if no resize occured
	bool SubmitFrame();

	void BeginFrameTimer() { m_profiler.BeginFrame(); }
	void EndFrameTimer() { m_profiler.EndFrame(); }

	Profiler& GetProfiler() { return m_profiler; }
	RD::RenderToggles& GetRenderToggles() { return m_profiler.debugToggles; }
	FrameStats& GetFrameStats() { return m_profiler.getStats(); }

	PipelineManager& GetPipelineManager() { return *m_pipelineManager; }

	const DescriptorManager& GetDescriptorManager() { return *m_descriptorManager; }
	const Device& GetDevice() { return *m_device; }
	const Swapchain& GetSwapchain() { return m_swapchain; }

	// Updates swapchain size and any resources that depend on draw extent size.
	void UpdateDrawExtentUsage(Extents2D newWindowExtent);

	void TickVramUsage();

	Extents2D GetDrawExtent() const { return m_drawExtent; }

	uint32_t GetFrameNumber() const { return m_frameNumber; }
	bool IsFirstFrame() const noexcept { return m_frameNumber == 0; }

	const std::vector<uint32_t>& GetMaterialFlagsById() { return m_materialFlagsIDs; }

	void UpdateRendererContext(GLFWwindow* window);

	void ResetFrameStats() { m_profiler.ResetDrawCalls(); m_profiler.ResetPassStats(); }
	void StartTimer() { m_profiler.StartTimer(); }
	void EndAssetTimer();
	void EndSceneUpdateTimer() { m_profiler.getStats().sceneUpdateTime.Add(m_profiler.EndTimerMS()); }
	void EndDrawTimer() { m_profiler.getStats().drawTime.Add(m_profiler.EndTimerMS()); }

private:
	uint32_t m_frameNumber = 0;
	uint32_t m_framesInFlight = 0;

	Extents2D m_drawExtent;
	void SetDrawExtent(Extents2D extent) { m_drawExtent = extent; }

	RenderPassExecutionContext m_renderPassExecutionContext;
	RD::RenderStateInfo m_renderGraphState;
	RenderGraph m_renderGraph;

	void CreateRenderGraph();
	void DestroyRenderGraph();

	void InitFrameResources();
	void CleanupFrameResources();

	void CreateOBBLineBuffer(FrameContext& frameCtx);

	void CheckGlobalDescriptorSetSync();

	void CheckCSMAtlasExtentUpdate();

	void BatchUploadTextures(
		std::vector<SceneUploadBatch>& batches,
		std::vector<std::shared_ptr<ModelAsset>>& assets,
		VkCommandBuffer cmd);
	void BatchUploadMeshes(
		std::vector<SceneUploadBatch>& batches,
		std::vector<std::shared_ptr<ModelAsset>>& assets,
		VkCommandBuffer cmd);
	void BatchUploadMaterials(
		std::vector<SceneUploadBatch>& batches,
		std::vector<std::shared_ptr<ModelAsset>>& assets);

	void UpdateGlobalBufferTable(VkCommandBuffer cmd);

	void FreeAllAssetTextures();

	void TimestampPoolStart(FrameContext& frameCtx);
	void TimestampPoolEnd(FrameContext& frameCtx);

	void BarrierDynamicBuffers(FrameContext& frameCtx);

	FrameContext& GetCurrentFrame()
	{
		return m_frameContexts[m_frameNumber % m_framesInFlight];
	}

	FrameContext& GetLastFrame()
	{
		uint32_t lastFrameNumber = m_frameNumber + m_framesInFlight - 1u;
		return m_frameContexts[lastFrameNumber % m_framesInFlight];
	}

	// Will vary, vsync is 2 contexts. When vsync off its 3.
	std::array<FrameContext, RD::MAX_FRAMES_IN_FLIGHT> m_frameContexts;

	BindlessBDATable m_globalAddressTable;
	std::vector<Material> m_materials;
	MeshRegistry m_registeredMeshes;

	BindlessImageTable m_bindlessImageTable;

	std::vector<uint32_t> m_materialFlagsIDs;

	DescriptorWriter m_mainWriter;

	std::unique_ptr<DescriptorManager> m_descriptorManager;
	std::unique_ptr<PipelineManager> m_pipelineManager;
	std::unique_ptr<Device> m_device;
	Swapchain m_swapchain;
	Allocator m_allocator;

	ClusterBufferSizes m_clusterBufferSizes;
	Cmaa2BufferSizes m_cmaa2BufferSizes;

	bool m_bHasDrawExtentResized = false;

	Profiler m_profiler;

	RD::ShadowQuality m_currentShadowQuality;

	void InitRenderSettings(
		bool enableLensFlare,
		bool enableChromaticAberration,
		bool enableShadows,
		bool enableSSS,
		bool enableVolumetrics,
		RD::AntiAliasingMethod aaMode,
		RD::AmbientOcclusionMethod aoMode,
		RD::ShadowQuality shadowQuality,
		bool enableProfilerView,
		bool enableSettings);

	// Must always initialize to read states
	bool m_bRenderTargetsLayoutsTransitioned = false;

	uint32_t m_activeEnvSet = UINT32_MAX;

	glm::vec4 m_luminanceSums[RD::MAX_LUMINANCE_GROUPS] = { glm::vec4(0.0f) };
};
