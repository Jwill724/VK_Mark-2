#include "pch.h"

#include "Renderer.h"
#include "backend/memory/Budgets.h"
#include "backend/Device.h"
#include "backend/PipelineManager.h"
#include "backend/DescriptorManager.h"
#include "backend/PhysicalDeviceSelector.h"
#include "backend/BufferBarriers.h"
#include "rendergraph/RenderPasses.h"
#include "scene/World.h"
#include "scene/LightingSystem.h"
#include "scene/DrawPreparation.h"
#include "scene/Scene.h"
#include "core/Window.h"
#include "core/JobSystem.h"
#include "core/Environment.h"
#include "core/asset/AssetUploadTypes.h"

#ifndef NDEBUG

#define RESIZE_TRACE(...) \
	do { \
		fmt::print(stderr, __VA_ARGS__); \
		fmt::print(stderr, "\n"); \
	} while (0)

#else

#define RESIZE_TRACE(...) ((void)0)

#endif

// TODO: List of shit that must get fixed.
// - TAA jitter on thin and edge geometry, influenced most with higher luminance.
//   Appears in transparent rendering and volumetrics passing through edges of geometry will jitter bad.
//   The jitter doesn't seem to fully adhere to the frame rate but jitter just becomes a bit slower/smoother at high fps.
//   Some mild ghosting with dynamic objects and some materials will have aliasing in motion, likely due to mip bias.

static_assert(sizeof(InstanceInput)   == SIZEOF_INSTANCE_INPUT);
static_assert(sizeof(DrawBin)         == SIZEOF_DRAW_BIN);
static_assert(sizeof(VkAccelerationStructureInstanceKHR) == SIZEOF_RT_INSTANCE);

inline constexpr bool LensFlareOn           = true;
inline constexpr bool ChromaticAberrationOn = true;
inline constexpr bool BloomOn               = true;
inline constexpr bool VolumetricsOn         = true;
inline constexpr bool ShadowsOn             = true;
inline constexpr bool rtReflectionsOn       = true;
inline constexpr bool ScreenSpaceShadowsOn  = true;
inline constexpr bool ProfilerViewOn        = false;
inline constexpr bool SettingsTabOn         = true;

void Renderer::Init(
	const Window& window,
	JobSystem& jobSystem)
{
	SetDrawExtent(window.GetExtent());

	InitRenderSettings(
		LensFlareOn,
		ChromaticAberrationOn,
		BloomOn,
		ShadowsOn,
		ScreenSpaceShadowsOn,
		VolumetricsOn,
		rtReflectionsOn,
		RD::AntiAliasingMethod::AA_TAA,
		RD::GIMethod::VBGI,
		RD::ShadowQuality::High,
		RD::SunShadowFilter::RT_SOFT,
		ProfilerViewOn,
		SettingsTabOn);

	// ==========================
	// === Vulkan state setup ===

	// ----------------
	// Device creation
	// ----------------
	m_device = std::make_unique<Device>();
	m_device->CreateInstance();
	m_device->CreateSurface(window.GetWindowHandle());

	auto deviceCandidate = PhysicalDeviceSelector::PickBest(
		m_device->GetContext().instance,
		m_device->GetSurface(),
		m_device->GetDeviceExtensions());

	m_device->InitLogical(deviceCandidate);

	m_profiler.SetGPUName(m_device->GetPhysicalDeviceName());

	m_device->InitThreadCommandPool(jobSystem.GetThreadCount());

#ifdef TRACY_ENABLE
	const auto& mainThread = jobSystem.GetMainContext();
	auto mainThreadGraphicsPool = m_device->GetThreadCommandPool(mainThread.threadID, QueueType::Graphics);
	auto mainThreadComputePool = m_device->GetThreadCommandPool(mainThread.threadID, QueueType::Compute);
	m_profiler.SetTracyGraphicsCmd(m_device->CreateCommandBuffer(mainThreadGraphicsPool));
	m_profiler.SetTracyComputeCmd(m_device->CreateCommandBuffer(mainThreadComputePool));

	m_profiler.InitTracyGraphics(
		m_device->GetContext().physicalDevice,
		m_device->GetContext().device,
		m_device->GetGraphicsQueue().GetQueue(),
		m_profiler.GetTracyGraphicsCmd());

	m_profiler.InitTracyCompute(
		m_device->GetContext().physicalDevice,
		m_device->GetContext().device,
		m_device->GetComputeQueue().GetQueue(),
		m_profiler.GetTracyComputeCmd(),
		jobSystem.GetThreadCount());
#endif

	// -------------------
	// Allocator creation
	// -------------------
	m_allocator.Init(m_device->GetContext());

	// ----------
	// Swapchain
	// ----------
	m_swapchain.Init(
		m_device->GetContext(),
		m_device->GetSurface(),
		m_device->GetSwapchainSupportDetails(),
		window.GetExtent());

	// ------------------------
	// Descriptor sets/layouts
	//-------------------------
	m_descriptorManager = std::make_unique<DescriptorManager>();
	m_descriptorManager->InitDescriptors(m_device->GetContext().device);

	// ----------
	// Pipelines
	//-----------
	m_pipelineManager = std::make_unique<PipelineManager>();
	m_pipelineManager->CreatePipelineLayout(
		m_device->GetContext().device,
		m_descriptorManager->GetDescriptorLayouts());
	m_pipelineManager->InitPipelines(m_device->GetContext().device);

	// ===========================
	// === Frame context setup ===
	InitFrameResources(jobSystem.GetThreadCount());

	const size_t totalFrameStaging =
		GPU_BYTES_INSTANCE_INPUT +
		GPU_BYTES_DRAW_BIN_KEYS + 
		GPU_BYTES_DYNAMIC_TRANSFORMS +
		GPU_BYTES_DYNAMIC_TRANSFORMS + // Motion matrices
		GPU_BYTES_LIGHTS +
		m_globalAddressTable.GPU_ADDRESS_TABLE_SIZE_GPU_BYTES;

	m_allocator.InitFrameStaging(totalFrameStaging, m_device->GetNonCoherentAtomSize());

	// =============================
	// === Global resource setup ===

	m_device->InitCrashMarkers(m_framesInFlight, 64);

	// --------
	// Buffers
	//---------

	m_globalAddressTable.Init(m_allocator);

	m_globalAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::Luminance,
		GPU_BYTES_LUMINANCE,
		m_allocator);

	m_globalAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::SHIrradiance,
		GPU_BYTES_SH_IRRADIANCE,
		m_allocator);

	m_globalAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::InstanceInputs,
		GPU_BYTES_INSTANCE_INPUT,
		m_allocator);

	m_globalAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::RTRows,
		GPU_BYTES_RT_ROWS,
		m_allocator);

	m_globalAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::StaticTransforms,
		GPU_BYTES_STATIC_TRANSFORMS,
		m_allocator);

	m_globalAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::DrawBinKeys,
		GPU_BYTES_DRAW_BIN_KEYS,
		m_allocator);

	// ===================
	// === Image setup ===

	m_bindlessImageTable.Init(
		{ m_drawExtent.Width(), m_drawExtent.Height(), 1u },
		Environment::_HDRPathCount,
		m_currentShadowQuality,
		m_device->GetContext().device,
		m_allocator);

	m_bindlessImageTable.PreallocateEquirects(Environment::_HDRPaths, m_allocator);

	// ===============================
	// === Global Data processing ====

	const size_t globalStagingSize = m_allocator.CalcBaseGlobalStagingSize(m_bindlessImageTable);
	m_allocator.InitGlobalStaging(globalStagingSize, m_device->GetNonCoherentAtomSize());

	jobSystem.SubmitJob([&](ThreadContext& threadCtx) {
		auto cmdpool = m_device->GetThreadCommandPool(threadCtx.threadID, QueueType::Graphics);

		m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
			{
				m_bindlessImageTable.UploadStaticTextures(m_allocator.GlobalStaging, cmd);

			}, cmdpool, QueueType::Graphics);
	});

	jobSystem.SubmitJob([&](ThreadContext& threadCtx) {
		auto cmdpool = m_device->GetThreadCommandPool(threadCtx.threadID, QueueType::Graphics);

		m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
			{
				m_bindlessImageTable.UploadEquirects(
					Environment::_HDRPaths,
					m_allocator,
					cmd);
			}, cmdpool, QueueType::Graphics);
	});

	jobSystem.Wait();

	m_device->SubmitDeferredCommands(QueueType::Graphics);

	m_allocator.GlobalStaging.Reset();

	const auto& shIrradianceBuffer = m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::SHIrradiance);

	// Global address table and luminance buffer upload
	jobSystem.SubmitJob([&, shIrradianceBuffer](ThreadContext& threadCtx) {
		auto cmdpool = m_device->GetThreadCommandPool(threadCtx.threadID, QueueType::Transfer);

		auto stageCopyLuminance = m_allocator.GlobalStaging.Stage(
			m_luminanceSums,
			GPU_BYTES_LUMINANCE,
			m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::Luminance).m_buffer);

		auto stageCopyShIrr = m_allocator.GlobalStaging.Stage(
			m_shIrradiance,
			GPU_BYTES_SH_IRRADIANCE,
			shIrradianceBuffer.m_buffer);

		auto stageCopyGlobalAddrTable = m_allocator.GlobalStaging.Stage(
			m_globalAddressTable.GetAddrPtrTable().data(),
			m_globalAddressTable.GPU_ADDRESS_TABLE_SIZE_GPU_BYTES,
			m_globalAddressTable.GetTableBuffer().m_buffer);

		m_allocator.GlobalStaging.Flush();

		m_device->RecordDeferredCommand([&,
			shIrradianceBuffer, stageCopyLuminance, stageCopyShIrr, stageCopyGlobalAddrTable](VkCommandBuffer cmd)
		{
			m_allocator.GlobalStaging.CopyCommand(cmd, stageCopyLuminance);
			m_allocator.GlobalStaging.CopyCommand(cmd, stageCopyShIrr);
			m_allocator.GlobalStaging.CopyCommand(cmd, stageCopyGlobalAddrTable);

			BufferBarriers::TransferReleaseOnGraphics(
				cmd,
				shIrradianceBuffer,
				m_device->GetContext());
			BufferBarriers::TransferReleaseOnGraphics(
				cmd,
				m_globalAddressTable.GetTableBuffer(),
				m_device->GetContext());
		}, cmdpool, QueueType::Transfer);
	});

	jobSystem.Wait();

	m_device->SubmitDeferredCommands(QueueType::Transfer);

	m_allocator.GlobalStaging.Reset();

	// ==========================
	// === Environment setup ====

	jobSystem.SubmitJob([&, shIrradianceBuffer](ThreadContext& threadCtx) {
		auto cmdpool = m_device->GetThreadCommandPool(threadCtx.threadID, QueueType::Graphics);

		std::vector<PipelineHandle> envPipelines = {
			m_pipelineManager->GetHandle(RD::Renderer_Pipeline::HDRToCubemap),
			m_pipelineManager->GetHandle(RD::Renderer_Pipeline::SHIrradiance),
			m_pipelineManager->GetHandle(RD::Renderer_Pipeline::SpecularPrefilter),
			m_pipelineManager->GetHandle(RD::Renderer_Pipeline::BRDFLUT) // Not apart of env set
		};
		m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
			{
				BufferBarriers::TransferWriteToComputeRead(
					cmd,
					m_globalAddressTable.GetTableBuffer(),
					m_device->GetContext());

				BufferBarriers::TransferWriteToComputeWrite(
					cmd,
					shIrradianceBuffer,
					m_device->GetContext());

				m_mainWriter.WriteBuffer(
					RD::ADDRESS_TABLE_BINDING,
					m_globalAddressTable.GetTableBuffer(),
					m_descriptorManager->GetGlobalSet());

				m_mainWriter.UpdateSet(
					m_device->GetContext().device,
					m_descriptorManager->GetGlobalSet());

				m_globalAddressTable.ClearDirty();

				m_descriptorManager->BindGlobalSetCompute(
					cmd,
					m_pipelineManager->GetGlobalLayout());

				BakeEnvironmentMaps(
					cmd,
					m_bindlessImageTable,
					envPipelines);

				BufferBarriers::ComputeWriteToRead(
					cmd,
					shIrradianceBuffer);

			}, cmdpool, QueueType::Graphics);
	});
	jobSystem.Wait();

	m_device->SubmitDeferredCommands(QueueType::Graphics);

	// ===============================
	// === Global descriptor setup ===

	jobSystem.SubmitJob([&](ThreadContext& threadCtx) {
		m_bindlessImageTable.BuildInitialCombinedSamplerArray();
		});

	jobSystem.SubmitJob([&](ThreadContext& threadCtx) {
		m_bindlessImageTable.BuildInitialSamplerCubeArray();
		});


	m_nrdReflectContext.Init(
		*m_device,
		m_allocator,
		{ (m_drawExtent.Width() + 1u) / 2u, (m_drawExtent.Height() + 1u) / 2u },
		NRDContext::DenoiserMode::Reflections);

	m_nrdShadowContext.Init(
		*m_device,
		m_allocator,
		m_drawExtent,
		NRDContext::DenoiserMode::Shadows);

	jobSystem.SubmitJob([&](ThreadContext& threadCtx) {
		auto cmdGfxPool = m_device->GetThreadCommandPool(threadCtx.threadID, QueueType::Graphics);
		m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
			{
				m_bindlessImageTable.TransitionRenderTargetsFromUndefined(cmd);
			}, cmdGfxPool, QueueType::Graphics);
		});

	jobSystem.SubmitJob([&](ThreadContext& threadCtx) {
		auto cmdCompPool = m_device->GetThreadCommandPool(threadCtx.threadID, QueueType::Compute);
		m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
			{
				m_nrdReflectContext.RecordPoolInit(cmd);
				m_nrdShadowContext.RecordPoolInit(cmd);
			}, cmdCompPool, QueueType::Compute);
		});

	jobSystem.Wait();

	m_device->SubmitDeferredCommands(QueueType::Graphics);
	m_device->SubmitDeferredCommands(QueueType::Compute);
	m_device->GetGraphicsQueue().WaitIdle();
	m_device->GetComputeQueue().WaitIdle();

	m_bindlessImageTable.FreeEquirects(m_allocator);

	m_profiler.SetVRAMUsage(m_allocator.GetTotalVRAMUsage());

	CreateRenderGraph();

	World::Init(m_bindlessImageTable);

	auto& forwardPush = m_profiler.forwardPush;
	forwardPush.flashlightCookieTexID = LightingSystem::_mainFlashLight.m_cookieGoboID;
	forwardPush.flashlightShadowMapID = LightingSystem::_mainFlashLight.m_shadowMapID;

	uint32_t brdfID = m_bindlessImageTable.GetStaticTexture(RD::Renderer_Texture::Brdf).m_bindlessID;

	forwardPush.brdfID = brdfID;
	m_profiler.reflectPush.brdfID = brdfID;

	m_profiler.lensFlareSettings.rainbowLUTIndex = m_bindlessImageTable.GetStaticTexture(RD::Renderer_Texture::RainbowLut).m_bindlessID;

	uint32_t hilbertCurveID = m_bindlessImageTable.GetStaticTexture(RD::Renderer_Texture::HilbertCurveLut).m_bindlessID;
	m_profiler.ssgiSettings.hilbertLutID = hilbertCurveID;
	m_profiler.reflectPush.hilbertLutID = hilbertCurveID;
	m_profiler.rtShadowPush.hilbertLutID = hilbertCurveID;
}

// Only called after swapchain presented and queue wait
void Renderer::CheckCSMAtlasExtentUpdate()
{
	if (m_currentShadowQuality == m_profiler.shadowQuality) return;

	StallDevice();
	m_currentShadowQuality = m_profiler.shadowQuality;
	m_bindlessImageTable.UpdateCSMAtlasExtent(m_currentShadowQuality, m_allocator);

	if (m_bindlessImageTable.IsShadowAtlasCached()) return;

	m_renderGraph.NotifyLayout(
		RD::Renderer_RenderTarget::DirectionalCSMAtlas,
		RD::ImageAccess::Undefined);

	const auto& csmAtlas = m_bindlessImageTable.GetRenderTarget(RD::Renderer_RenderTarget::DirectionalCSMAtlas);
	World::GetScene().InitCSMInfo(csmAtlas.Width(), csmAtlas.Height(), csmAtlas.m_bindlessID);
}

void Renderer::CheckGlobalDescriptorSetSync()
{
	bool updateSet = false;

	if (m_globalAddressTable.IsTableDirty())
	{
		m_mainWriter.WriteBuffer(
			RD::ADDRESS_TABLE_BINDING,
			m_globalAddressTable.GetTableBuffer(),
			m_descriptorManager->GetGlobalSet());

		m_globalAddressTable.ClearDirty();

		updateSet = true;
	}

	if (m_bindlessImageTable.IsTableDirty())
	{
		m_mainWriter.WriteBindlessImages(
			m_bindlessImageTable.GetCombinedSamplerArray(),
			RD::GLOBAL_BINDING_COMBINED_SAMPLER,
			m_descriptorManager->GetGlobalSet());

		m_mainWriter.WriteBindlessImages(
			m_bindlessImageTable.GetSamplerCubeArray(),
			RD::GLOBAL_BINDING_SAMPLER_CUBE,
			m_descriptorManager->GetGlobalSet());

		m_bindlessImageTable.ClearDirty();

		updateSet = true;
	}

	static RD::RenderToggles last{};
	const RD::RenderToggles& cur = m_profiler.debugToggles;

	if (memcmp(&last, &cur, sizeof(RD::RenderToggles)) != 0)
	{
		m_mainWriter.WriteInlineUniform(
			m_device->GetContext().device,
			m_descriptorManager->GetGlobalSet(),
			&cur,
			static_cast<uint32_t>(sizeof(RD::RenderToggles)));
		last = cur;
	}

	if (updateSet)
	{
		m_mainWriter.UpdateSet(m_device->GetContext().device, m_descriptorManager->GetGlobalSet());
	}
}

void Renderer::InitFrameResources(uint32_t threadCount)
{
	m_framesInFlight = m_swapchain.GetImageCount();
	fmt::println("Frames in flight:[{}]", m_framesInFlight);

	m_rtRayListLayout.Update(m_drawExtent.Width(), m_drawExtent.Height());
	m_clusterBufferSizes.UpdateClusterBufferSizes(m_drawExtent.Width(), m_drawExtent.Height());

	for (uint32_t i = 0; i < m_framesInFlight; ++i)
	{
		m_frameContexts[i].Init(
			i,
			threadCount,
			*m_device,
			*m_descriptorManager,
			m_allocator);

		m_frameContexts[i].m_cachedDrawExtent = m_drawExtent;

		m_frameContexts[i].CreateClusterBuffers(m_clusterBufferSizes, m_allocator);
		m_frameContexts[i].CreateRTRayListBuffer(m_rtRayListLayout, m_allocator);
	}
}

void Renderer::CleanupFrameResources()
{
	for (uint32_t i = 0; i < m_framesInFlight; ++i)
	{
		m_frameContexts[i].Cleanup(
			m_device->GetContext(),
			m_allocator);
	}
}

void Renderer::CreateRenderGraph()
{
	const uint32_t gfxFamily     = m_device->GetGraphicsQueue().GetFamilyIndex();
	const uint32_t computeFamily = m_device->GetComputeQueue().GetFamilyIndex();

	const bool bDedicatedCompute = (gfxFamily != computeFamily);

	if (!bDedicatedCompute)
	{
		fmt::println(
			"[Renderer] No dedicated compute queue family (graphics and compute "
			"both on family {}). Async compute disabled; the graph will use the "
			"single-batch path.", gfxFamily);
	}

	m_renderGraph.Build(*m_pipelineManager, m_drawExtent, bDedicatedCompute);
}

void Renderer::DestroyRenderGraph()
{
	m_renderGraph.Shutdown();
}

void Renderer::EndAssetTimer()
{
	auto elapsed = m_profiler.EndTimerSec();
	// Cheap check for now
	if (!m_materials.empty() && m_registeredMeshes.GetMeshCount() > 0)
	{
		fmt::print("Asset loading completed in {:.3f} seconds.\n\n", elapsed);
	}
}

void Renderer::InitRenderSettings(
	bool enableLensFlare,
	bool enableChromaticAberration,
	bool enableBloom,
	bool enableShadows,
	bool enableSSS,
	bool enableVolumetrics,
	bool enableRTReflections,
	RD::AntiAliasingMethod aaMode,
	RD::GIMethod giMode,
	RD::ShadowQuality shadowQuality,
	RD::SunShadowFilter sunShadowFilter,
	bool enableProfilerView,
	bool enableSettings)
{
	RD::RenderToggles& toggles = m_profiler.debugToggles;

	toggles.enableBloom               = enableBloom               ? 1u : 0u;
	toggles.enableLensFlare           = enableLensFlare           ? 1u : 0u;
	toggles.enableChromaticAberration = enableChromaticAberration ? 1u : 0u;
	toggles.enableShadows             = enableShadows             ? 1u : 0u;
	toggles.enableSSS                 = enableSSS                 ? 1u : 0u;
	toggles.enableVolumetrics         = enableVolumetrics         ? 1u : 0u;
	toggles.enableRTReflections       = enableRTReflections       ? 1u : 0u;

	toggles.bloomIntensity = 0.04;

	toggles.aaMode = static_cast<uint32_t>(aaMode);
	toggles.giMode = static_cast<uint32_t>(giMode);

	toggles.enableProfilerView = enableProfilerView ? 1u : 0u;
	toggles.enableSettings     = enableSettings     ? 1u : 0u;

	m_currentShadowQuality = shadowQuality;
	m_profiler.shadowQuality = shadowQuality;
	toggles.sunShadowFilter = static_cast<uint32_t>(sunShadowFilter);

	toggles.depthScale = 1.0f / World::GetScene().GetCamera().GetFarClip();
}

void Renderer::UploadScenes(std::vector<SceneUploadBatch>&& batches)
{
	if (batches.empty()) return;

	// Build one ModelAsset per batch upfront
	std::vector<std::shared_ptr<ModelAsset>> assets;
	assets.reserve(batches.size());

	for (auto& batch : batches)
	{
		auto asset             = std::make_shared<ModelAsset>();
		asset->sceneID         = batch.sceneID;
		asset->sceneName       = batch.sceneName;
		asset->lifetime        = batch.lifetime;
		asset->instances       = std::move(batch.instances);
		asset->nodeTransforms  = std::move(batch.nodeTransforms);
		asset->localToNodeSlot = std::move(batch.localToNodeSlot);
		asset->lights          = std::move(batch.lights);
		asset->virtualInstance = batch.virtualInstance;
		assets.push_back(asset);
	}

	// ---- Compute total staging size needed ----
	size_t totalTexBytes     = 0;
	size_t totalVtxBytes     = 0;
	size_t totalIdxBytes     = 0;
	size_t totalMeshBytes    = 0;
	size_t totalMatBytes     = 0;
	size_t totalMeshletBytes = 0;
	size_t totalMLVertBytes  = 0;
	size_t totalMLTriBytes   = 0;

	auto& assetCounts = m_profiler.assetCounts;

	for (auto& batch : batches)
	{
		for (const auto& t : batch.textures)
			if (t.IsValid())
				totalTexBytes += AllocatedBuffer::AlignUp(t.pixelData.size(), 4u);

		// TODO: Eventually add a way to subtract from this initial count
		assetCounts.totalVertexCount += static_cast<uint32_t>(batch.vertices.size());
		assetCounts.totalIndexCount += static_cast<uint32_t>(batch.indices.size());
		assetCounts.totalMeshCount += static_cast<uint32_t>(batch.meshes.size());
		assetCounts.totalMaterialCount += static_cast<uint32_t>(batch.materials.size());

		totalVtxBytes     += batch.vertices.size()         * sizeof(Vertex);
		totalIdxBytes     += batch.indices.size()          * sizeof(uint32_t);
		totalMeshBytes    += batch.meshes.size()           * sizeof(Mesh);
		totalMatBytes     += batch.materials.size()        * sizeof(Material);
		totalMeshletBytes += batch.meshlets.size()         * sizeof(Meshlet);
		totalMLVertBytes  += batch.meshletVertices.size()  * sizeof(uint32_t);
		totalMLTriBytes   += batch.meshletTriangles.size() * sizeof(uint8_t);
	}

	const size_t totalNeeded =
		totalTexBytes + totalVtxBytes + totalIdxBytes
		+ totalMeshBytes + totalMatBytes + totalMeshletBytes
		+ totalMLTriBytes + totalMLVertBytes
		+ m_globalAddressTable.GPU_ADDRESS_TABLE_SIZE_GPU_BYTES;

	if (totalNeeded > m_allocator.GlobalStaging.GetCapacity())
		m_allocator.ResetGlobalStaging(totalNeeded, m_device->GetNonCoherentAtomSize());

	// ---- Textures — graphics queue (mip gen needs blit) ----
	{
		auto cmdPool = m_device->GetThreadCommandPool(
			JobSystem::RENDER_THREAD, QueueType::Graphics);

		m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
		{
			BatchUploadTextures(batches, assets, cmd);
		}, cmdPool, QueueType::Graphics);

		m_device->SubmitDeferredCommands(QueueType::Graphics);
		m_device->GetGraphicsQueue().WaitIdle();
		m_allocator.GlobalStaging.Reset();
	}

	// ---- Meshes — transfer queue ----
	{
		ASSERT(totalVtxBytes > 0);
		ASSERT(totalIdxBytes > 0);
		ASSERT(totalMeshBytes > 0);
		ASSERT(totalMeshletBytes > 0);
		ASSERT(totalMLVertBytes > 0);
		ASSERT(totalMLTriBytes > 0);

		// Allocate singular global buffers sized for ALL scenes combined
		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::Vertex, totalVtxBytes, m_allocator);

		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::Index, totalIdxBytes, m_allocator);

		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::Mesh, totalMeshBytes, m_allocator);

		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::Meshlet, totalMeshletBytes, m_allocator);

		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::MeshletVertices, totalMLVertBytes, m_allocator);

		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::MeshletTriangles, totalMLTriBytes, m_allocator);

		auto cmdPool = m_device->GetThreadCommandPool(
			JobSystem::RENDER_THREAD, QueueType::Transfer);

		m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
		{
			BatchUploadMeshes(batches, assets, cmd);
		}, cmdPool, QueueType::Transfer);

		m_device->SubmitDeferredCommands(QueueType::Transfer);
		m_device->GetTransferQueue().WaitIdle();
		m_allocator.GlobalStaging.Reset();
	}

	// ---- BLAS builds -----
	{
		auto cmdPool = m_device->GetThreadCommandPool(
			JobSystem::RENDER_THREAD, QueueType::Graphics);

		AllocatedBuffer scratch{};

		m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
		{
			m_blasAddresses = BuildMeshBLAS(cmd, scratch);
		}, cmdPool, QueueType::Graphics);

		m_device->SubmitDeferredCommands(QueueType::Graphics);
		m_device->GetGraphicsQueue().WaitIdle();
		m_allocator.FreeBuffer(scratch);

		if (!m_blasAddresses.empty())
		{
			m_globalAddressTable.AddGPUBufferToAddressTable(
				RD::Renderer_Buffer::BLASAddresses,
				m_blasAddresses.size() * sizeof(uint64_t),
				m_allocator);

			auto transferPool = m_device->GetThreadCommandPool(
				JobSystem::RENDER_THREAD, QueueType::Transfer);

			m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
			{
				auto write = m_allocator.GlobalStaging.Stage(
					m_blasAddresses.data(),
					m_blasAddresses.size() * sizeof(uint64_t),
					m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::BLASAddresses).m_buffer);

				m_allocator.GlobalStaging.Flush();
				m_allocator.GlobalStaging.CopyCommand(cmd, write);

				BufferBarriers::TransferReleaseOnGraphics(
					cmd,
					m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::BLASAddresses),
					m_device->GetContext());
			}, transferPool, QueueType::Transfer);

			m_device->SubmitDeferredCommands(QueueType::Transfer);
			m_device->GetTransferQueue().WaitIdle();
			m_allocator.GlobalStaging.Reset();
		}
	}

	// ---- Materials — transfer queue ----
	{
		ASSERT(totalMatBytes > 0);
		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::Material, totalMatBytes, m_allocator);

		BatchUploadMaterials(batches, assets);
	}

	// ---- Address table — single upload covering all new buffer pointers ----
	{
		auto cmdPool = m_device->GetThreadCommandPool(
			JobSystem::RENDER_THREAD, QueueType::Transfer);

		m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
		{
			auto write = m_allocator.GlobalStaging.Stage(
				m_globalAddressTable.GetAddrPtrTable().data(),
				m_globalAddressTable.GPU_ADDRESS_TABLE_SIZE_GPU_BYTES,
				m_globalAddressTable.GetTableBuffer().m_buffer);

			m_allocator.GlobalStaging.Flush();
			m_allocator.GlobalStaging.CopyCommand(cmd, write);
		}, cmdPool, QueueType::Transfer);

		m_device->SubmitDeferredCommands(QueueType::Transfer);
		m_device->GetTransferQueue().WaitIdle();
		m_allocator.GlobalStaging.Reset();
	}

	// Register all assets into World in one pass
	for (auto& asset : assets)
		World::OnSceneLoaded(asset);

	fmt::println("[Renderer] Uploaded {} scene(s).", batches.size());
}

void Renderer::BatchUploadTextures(
	std::vector<SceneUploadBatch>&              batches,
	std::vector<std::shared_ptr<ModelAsset>>&   assets,
	VkCommandBuffer                             cmd)
{
	for (size_t b = 0; b < batches.size(); ++b)
	{
		auto& batch = batches[b];
		auto& asset = *assets[b];

		if (batch.textures.empty()) continue;

		asset.ownedTextureSlots = m_bindlessImageTable.UploadAssetTextures(
			batch,
			m_device->GetContext().device,
			m_allocator,
			m_allocator.GlobalStaging,
			cmd);

		asset.textureBindlessIDs.resize(batch.textures.size(), UINT32_MAX);
		for (uint32_t i = 0; i < static_cast<uint32_t>(batch.textures.size()); ++i)
			asset.textureBindlessIDs[i] = batch.textures[i].bindlessID;
	}
}

void Renderer::BatchUploadMeshes(
	std::vector<SceneUploadBatch>&              batches,
	std::vector<std::shared_ptr<ModelAsset>>&   assets,
	VkCommandBuffer                             cmd)
{
	const auto& vtxBuf          = m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::Vertex);
	const auto& idxBuf          = m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::Index);
	const auto& meshBuf         = m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::Mesh);
	const auto& meshletBuf      = m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::Meshlet);
	const auto& meshletVertsBuf = m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::MeshletVertices);
	const auto& meshletTrisBuf  = m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::MeshletTriangles);

	// Running cursors — each scene appends after the previous
	uint32_t globalVertexCursor       = 0;
	uint32_t globalIndexCursor        = 0;
	uint32_t globalMeshCursor         = 0;
	uint32_t globalMeshletCursor      = 0;
	uint32_t globalMeshletVertsCursor = 0;
	uint32_t globalMeshletTrisCursor  = 0;

	// Staging offsets into the single global buffer
	size_t stagingVtxOffset          = 0;
	size_t stagingIdxOffset          = 0;
	size_t stagingMeshOffset         = 0;
	size_t stagingMeshletOffset      = 0;
	size_t stagingMeshletVertsOffset = 0;
	size_t stagingMeshletTrisOffset  = 0;

	// Collect all GPU mesh structs into one flat array
	std::vector<Mesh> allGpuMeshes;
	std::vector<Meshlet> allMeshlets;

	for (size_t b = 0; b < batches.size(); ++b)
	{
		auto& batch = batches[b];
		auto& asset = *assets[b];

		if (batch.meshes.empty()) continue;

		const uint32_t localMeshBase         = globalMeshCursor;
		const uint32_t localVertBase         = globalVertexCursor;
		const uint32_t localIndexBase        = globalIndexCursor;
		const uint32_t localMeshletBase      = globalMeshletCursor;
		const uint32_t localMeshletVertsBase = globalMeshletVertsCursor;
		const uint32_t localMeshletTrisBase  = globalMeshletTrisCursor;

		asset.meshGlobalIDs.reserve(batch.meshes.size());

		for (auto& md : batch.meshes)
		{
			// Adjust to global offsets
			md.firstIndex          += localIndexBase;
			md.vertexOffset        += localVertBase;
			md.shadowFirstIndex    += localIndexBase;
			md.meshletOffset       += localMeshletBase;
			md.shadowMeshletOffset += localMeshletBase;

			Mesh gpuMesh{};
			gpuMesh.firstIndex                  = md.firstIndex;
			gpuMesh.indexCount                  = md.indexCount;
			gpuMesh.vertexOffset                = md.vertexOffset;
			gpuMesh.vertexCount                 = md.vertexCount;
			gpuMesh.shadowFirstIndex            = md.shadowFirstIndex;
			gpuMesh.shadowIndexCount            = md.shadowIndexCount;
			gpuMesh.localAABB                   = md.localAABB;
			gpuMesh.localBoundingRadius         = md.localBoundingRadius;
			gpuMesh.meshletCount                = md.meshletCount;
			gpuMesh.meshletOffset               = md.meshletOffset;
			gpuMesh.shadowMeshletCount          = md.shadowMeshletCount;
			gpuMesh.shadowMeshletOffset         = md.shadowMeshletOffset;
			gpuMesh.meshletVisibilityBase       = md.meshletVisibilityBase;

			const uint32_t globalID = m_registeredMeshes.RegisterMesh(gpuMesh);
			md.globalMeshID = globalID;
			asset.meshGlobalIDs.push_back(globalID);
			allGpuMeshes.push_back(gpuMesh);
		}

		for (auto& ml : batch.meshlets)
		{
			ml.vertexOffset   += localMeshletVertsBase;
			ml.triangleOffset += localMeshletTrisBase;
			allMeshlets.push_back(ml);
		}

		m_registeredMeshes.ResizeMeshLods();
		for (uint32_t i = 0; i < static_cast<uint32_t>(batch.meshes.size()); ++i)
		{
			const auto& md = batch.meshes[i];
			auto& lods = m_registeredMeshes.GetLodsMutable()[md.globalMeshID];

			auto resolve = [&](uint32_t localIdx) -> uint32_t
			{
				if (localIdx == UINT32_MAX) return md.globalMeshID;
				return batch.meshes[localIdx].globalMeshID;
			};

			lods.lod0       = resolve(md.lod0);
			lods.lod1       = resolve(md.lod1);
			lods.lod2       = resolve(md.lod2);
			lods.lod3       = resolve(md.lod3);
			lods.shadowLod0 = resolve(md.shadowLod0);
			lods.shadowLod1 = resolve(md.shadowLod1);
			lods.shadowLod2 = resolve(md.shadowLod2);
			lods.flags      = md.flags;
		}

		// Resolve instance local mesh -> global mesh ID
		for (auto& inst : asset.instances)
			if (inst.localMeshIdx != UINT32_MAX &&
				inst.localMeshIdx < static_cast<uint32_t>(batch.meshes.size()))
				inst.localMeshIdx = batch.meshes[inst.localMeshIdx].globalMeshID;

		globalVertexCursor       += static_cast<uint32_t>(batch.vertices.size());
		globalIndexCursor        += static_cast<uint32_t>(batch.indices.size());
		globalMeshCursor         += static_cast<uint32_t>(batch.meshes.size());
		globalMeshletCursor      += static_cast<uint32_t>(batch.meshlets.size());
		globalMeshletVertsCursor += static_cast<uint32_t>(batch.meshletVertices.size());
		globalMeshletTrisCursor  += static_cast<uint32_t>(batch.meshletTriangles.size());
	}

	// Stage all scenes into the global buffers in one contiguous write per buffer
	size_t vtxOff = 0, idxOff = 0, mlVertOff = 0, mlTrisOff = 0;

	for (auto& batch : batches)
	{
		if (batch.vertices.empty() && batch.meshletVertices.empty()) continue;

		const size_t vBytes = batch.vertices.size() * sizeof(Vertex);
		const size_t iBytes = batch.indices.size()  * sizeof(uint32_t);
		const size_t mlVertBytes = batch.meshletVertices.size() * sizeof(uint32_t);
		const size_t mlTrisBytes = batch.meshletTriangles.size() * sizeof(uint8_t);

		// Stage directly into the correct offset of the global GPU buffer
		auto vtxWrite = m_allocator.GlobalStaging.Stage(
			batch.vertices.data(), vBytes, vtxBuf.m_buffer, vtxOff);
		auto idxWrite = m_allocator.GlobalStaging.Stage(
			batch.indices.data(), iBytes, idxBuf.m_buffer, idxOff);

		auto mlVertWrite = m_allocator.GlobalStaging.Stage(
			batch.meshletVertices.data(), mlVertBytes, meshletVertsBuf.m_buffer, mlVertOff);
		auto mlTrisWrite = m_allocator.GlobalStaging.Stage(
			batch.meshletTriangles.data(), mlTrisBytes, meshletTrisBuf.m_buffer, mlTrisOff);

		m_allocator.GlobalStaging.CopyCommand(cmd, vtxWrite);
		m_allocator.GlobalStaging.CopyCommand(cmd, idxWrite);
		
		m_allocator.GlobalStaging.CopyCommand(cmd, mlVertWrite);
		m_allocator.GlobalStaging.CopyCommand(cmd, mlTrisWrite);

		vtxOff += vBytes;
		idxOff += iBytes;
		mlVertOff += mlVertBytes;
		mlTrisOff += mlTrisBytes;

		batch.vertices.clear(); batch.vertices.shrink_to_fit();
		batch.indices.clear();  batch.indices.shrink_to_fit();
		batch.meshletVertices.clear(); batch.meshletVertices.shrink_to_fit();
		batch.meshletTriangles.clear();  batch.meshletTriangles.shrink_to_fit();
	}

	// Stage the combined mesh array — already adjusted to global offsets
	if (!allGpuMeshes.empty())
	{
		const size_t totalMeshBytes = allGpuMeshes.size() * sizeof(Mesh);
		auto meshWrite = m_allocator.GlobalStaging.Stage(
			allGpuMeshes.data(), totalMeshBytes, meshBuf.m_buffer);
		m_allocator.GlobalStaging.Flush();
		m_allocator.GlobalStaging.CopyCommand(cmd, meshWrite);
	}

	if (!allMeshlets.empty())
	{
		const size_t totalMeshletBytes = allMeshlets.size() * sizeof(Meshlet);
		auto meshletWrite = m_allocator.GlobalStaging.Stage(
			allMeshlets.data(), totalMeshletBytes, meshletBuf.m_buffer);
		m_allocator.GlobalStaging.Flush();
		m_allocator.GlobalStaging.CopyCommand(cmd, meshletWrite);
	}
}

void Renderer::BatchUploadMaterials(
	std::vector<SceneUploadBatch>&              batches,
	std::vector<std::shared_ptr<ModelAsset>>&   assets)
{
	std::vector<Material> allGpuMaterials;

	for (size_t b = 0; b < batches.size(); ++b)
	{
		auto& batch = batches[b];
		auto& asset = *assets[b];

		if (batch.materials.empty()) continue;

		asset.materialGlobalIDs.reserve(batch.materials.size());

		auto resolve = [&](uint32_t localIdx, RD::Renderer_Texture errorTex) -> uint32_t
		{
			if (localIdx == UINT32_MAX ||
				localIdx >= static_cast<uint32_t>(asset.textureBindlessIDs.size()))
				return m_bindlessImageTable.GetStaticTexture(errorTex).m_bindlessID;
			return asset.textureBindlessIDs[localIdx];
		};

		for (auto& desc : batch.materials)
		{
			Material mat{};
			mat.albedoID            = resolve(desc.albedoTexIdx,     RD::Renderer_Texture::White);
			mat.metalRoughnessID    = resolve(desc.metalRoughTexIdx, RD::Renderer_Texture::White);
			mat.normalID            = resolve(desc.normalTexIdx,     RD::Renderer_Texture::Normal);
			mat.emissiveID          = resolve(desc.emissiveTexIdx,   RD::Renderer_Texture::Dummy);
			mat.colorFactor         = desc.colorFactor;
			mat.metalRoughFactors   = desc.metalRoughFactors;
			mat.emissiveColor       = desc.emissiveColor;
			mat.emissiveStrength    = desc.emissiveStrength;
			mat.alphaCutoff         = desc.alphaCutoff;
			mat.normalScale         = desc.normalScale;
			mat.ior                 = desc.ior;
			mat.specularFactor      = desc.specularFactor;
			mat.clearcoatFactor     = desc.clearcoatFactor;
			mat.clearcoatRough      = desc.clearcoatRough;
			mat.diffuseTransFactor  = desc.diffuseTransFactor;
			mat.transmissionFactor  = desc.transmissionFactor;
			mat.sheenColor          = desc.sheenColor;
			mat.sheenRough          = desc.sheenRough;
			mat.shadingModel        = desc.shadingModel;
			mat.thicknessFactor     = desc.thicknessFactor;
			mat.attenuationColor    = desc.attenuationColor;
			mat.attenuationDistance = desc.attenuationDistance;

			const uint32_t globalID = static_cast<uint32_t>(m_materials.size());
			desc.globalMaterialID   = globalID;
			asset.materialGlobalIDs.push_back(globalID);
			m_materials.push_back(mat);
			allGpuMaterials.push_back(mat);

			if (globalID >= static_cast<uint32_t>(m_materialFlagsIDs.size()))
				m_materialFlagsIDs.resize(static_cast<size_t>(globalID + 1), 0u);
			m_materialFlagsIDs[globalID] = desc.flags;
		}

		// Resolve instance local material -> global material ID
		for (auto& inst : asset.instances)
			if (inst.localMaterialIdx != UINT32_MAX &&
				inst.localMaterialIdx < static_cast<uint32_t>(batch.materials.size()))
				inst.localMaterialIdx = batch.materials[inst.localMaterialIdx].globalMaterialID;
	}

	if (allGpuMaterials.empty()) return;

	const size_t totalMatBytes = allGpuMaterials.size() * sizeof(Material);
	const auto&  matBuf        = m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::Material);

	auto cmdPool = m_device->GetThreadCommandPool(
		JobSystem::RENDER_THREAD, QueueType::Transfer);

	m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
	{
		auto matWrite = m_allocator.GlobalStaging.Stage(
			allGpuMaterials.data(), totalMatBytes, matBuf.m_buffer);
		m_allocator.GlobalStaging.Flush();
		m_allocator.GlobalStaging.CopyCommand(cmd, matWrite);
	}, cmdPool, QueueType::Transfer);

	m_device->SubmitDeferredCommands(QueueType::Transfer);
	m_device->GetTransferQueue().WaitIdle();
	m_allocator.GlobalStaging.Reset();
}


std::vector<uint64_t> Renderer::BuildMeshBLAS(VkCommandBuffer cmd, AllocatedBuffer& outScratch)
{
	const auto& meshes = m_registeredMeshes.GetMeshes();
	const auto& lods   = m_registeredMeshes.GetLods();

	const VkDeviceAddress vtxAddr = m_globalAddressTable
		.GetGPUBuffer(RD::Renderer_Buffer::Vertex).m_address;
	const VkDeviceAddress idxAddr = m_globalAddressTable
		.GetGPUBuffer(RD::Renderer_Buffer::Index).m_address;

	std::vector<bool> needsBLAS(meshes.size(), false);
	for (size_t i = 0; i < meshes.size(); ++i)
		if ((lods[i].flags & MESH_FLAG_IS_LOD_VARIANT) == 0u)
			needsBLAS[i] = true;

	std::vector<uint32_t> buildList;
	for (size_t i = 0; i < meshes.size(); ++i)
		if (needsBLAS[i] && meshes[i].indexCount >= 3)
			buildList.push_back(static_cast<uint32_t>(i));

	ASSERT(!buildList.empty());

	std::vector<VkAccelerationStructureGeometryKHR> geoms(buildList.size());
	std::vector<VkAccelerationStructureBuildGeometryInfoKHR> builds(buildList.size());
	std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges(buildList.size());
	std::vector<VkDeviceSize> sizes(buildList.size());

	VkDeviceSize totalASBytes = 0;
	VkDeviceSize maxScratch   = 0;

	for (size_t i = 0; i < buildList.size(); ++i)
	{
		const Mesh& m = meshes[buildList[i]];

		auto& g = geoms[i];
		g = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
		g.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
		g.flags        = 0;

		auto& tri = g.geometry.triangles;
		tri.sType        = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
		tri.pNext        = nullptr;
		tri.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
		tri.vertexData.deviceAddress = vtxAddr + static_cast<VkDeviceSize>(m.vertexOffset) * sizeof(Vertex);
		tri.vertexStride = sizeof(Vertex);
		tri.maxVertex    = m.vertexCount - 1;
		tri.indexType    = VK_INDEX_TYPE_UINT32;
		tri.indexData.deviceAddress = idxAddr + static_cast<VkDeviceSize>(m.firstIndex) * sizeof(uint32_t);
		tri.transformData.deviceAddress = 0;

		auto& b = builds[i];
		b = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
		b.type          = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		b.flags         = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
		b.mode          = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
		b.geometryCount = 1;
		b.pGeometries   = &geoms[i];

		const uint32_t primCount = m.indexCount / 3u;
		ranges[i] = { primCount, 0, 0, 0 };

		VkAccelerationStructureBuildSizesInfoKHR sizeInfo{
			VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
		vkGetAccelerationStructureBuildSizesKHR(
			m_device->GetContext().device,
			VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
			&b, &primCount, &sizeInfo);

		sizes[i]      = AllocatedBuffer::AlignUp(sizeInfo.accelerationStructureSize, MIN_SSBO_ALIGNMENT_BYTES);
		totalASBytes += sizes[i];
		maxScratch    = std::max(maxScratch, sizeInfo.buildScratchSize);
	}

	const VkDeviceSize scratchAlign = m_device->GetMinASScratchAlignment();

	m_blasStorage = m_allocator.AllocateBuffer({
		totalASBytes, Vulkan_BufferUsage::AS_STORAGE, HeapType::GPU_Local, false, "BLASStorage" });

	AllocatedBuffer scratch = m_allocator.AllocateBuffer({
		maxScratch + scratchAlign, Vulkan_BufferUsage::AS_SCRATCH, HeapType::GPU_Local, false, "BLASScratch" });

	const VkDeviceAddress scratchAddr =
		(scratch.m_address + scratchAlign - 1) & ~(scratchAlign - 1);

	std::vector<uint64_t> blasAddresses(meshes.size(), 0ull);
	m_blasHandles.resize(buildList.size());

	VkDeviceSize offset = 0;

	for (size_t i = 0; i < buildList.size(); ++i)
	{
		VkAccelerationStructureCreateInfoKHR ci{
			VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
		ci.buffer = m_blasStorage.m_buffer;
		ci.offset = offset;
		ci.size   = sizes[i];
		ci.type   = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

		VK_CHECK(vkCreateAccelerationStructureKHR(
			m_device->GetContext().device, &ci, nullptr, &m_blasHandles[i]));

		offset += sizes[i];

		builds[i].dstAccelerationStructure  = m_blasHandles[i];
		builds[i].scratchData.deviceAddress = scratchAddr;

		ASSERT((scratchAddr % scratchAlign) == 0);
		ASSERT(scratchAddr + maxScratch <= scratch.m_address + scratch.m_bytesSize);

		const VkAccelerationStructureBuildRangeInfoKHR* pRange = &ranges[i];
		vkCmdBuildAccelerationStructuresKHR(cmd, 1, &builds[i], &pRange);
		BufferBarriers::ASBuildToASBuild(cmd);

		VkAccelerationStructureDeviceAddressInfoKHR addrInfo{
			VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
		addrInfo.accelerationStructure = m_blasHandles[i];

		blasAddresses[buildList[i]] = vkGetAccelerationStructureDeviceAddressKHR(
			m_device->GetContext().device, &addrInfo);
	}

	outScratch = scratch;
	return blasAddresses;
}



// ===============================================
// ===============================================
// ===============================================
// ===============================================
// ============= RUNTIME RENDERING ===============

void Renderer::UpdateRendererContext(GLFWwindow* window)
{
	auto& frameCtx = GetCurrentFrame();

	auto& debug = m_profiler.debugToggles;

	const auto& scene = World::GetScene();
	const auto& sceneData = scene.GetSceneData();

	UpdateShadowMode();

	World::UpdateWorldState(
		m_frameNumber,
		m_drawExtent,
		frameCtx,
		m_allocator,
		m_profiler,
		window,
		m_renderGraphState.TemporalAllowed());

	frameCtx.SetTemporalResult(scene.GetTemporalResult() && !IsFirstFrame());
	frameCtx.SetHiZValidResult(scene.GetHiZTemporalResult());

	m_renderGraphState.SetTemporalIndex(static_cast<uint64_t>(sceneData.temporal.x));

	uint64_t noiseIndex = m_renderGraphState.GetTemporalIndex() % 64u;

	bool instanceUploadNeeded = false;

	instanceUploadNeeded = DrawPreparation::SyncInstanceInputs(
		World::GetInstanceState(),
		scene,
		World::_loadedScenes,
		m_registeredMeshes.GetMeshes(),
		m_registeredMeshes.GetLods(),
		m_materialFlagsIDs,
		m_blasAddresses);

	frameCtx.FlagInstanceInputUpload(instanceUploadNeeded);

	const bool rtDirty = instanceUploadNeeded || scene.HasDynamicTransformChanges();

	if (rtDirty)
	{
		for (uint32_t i = 0; i < m_framesInFlight; ++i)
			m_frameContexts[i].MarkTlasDirty();
	}

	if (frameCtx.IsInstanceInputsUploadNeeded())
		m_drawBinTableBuild = DrawPreparation::BuildDrawBinTable(World::GetInstanceState().gpuInputs);

	auto& forwardPush = m_profiler.forwardPush;
	auto& lumaPush = m_profiler.lumaExposureSettings;
	auto& taaPush = m_profiler.taaSettings;
	auto& reflectPush = m_profiler.reflectPush;
	auto& rtShadowPush = m_profiler.rtShadowPush;
	auto& nrdRPush = m_profiler.nrdReflectPush;
	auto& nrdSPush = m_profiler.nrdShadowPush;

	glm::vec2 fullPixelSize = glm::vec2(sceneData.pixelSizes);

	const float rawDt = std::max(m_profiler.getStats().deltaSecondsRaw, 1e-5f);
	taaPush.invDeltaTime = 1.0f / rawDt;

	uint32_t tilesX = (m_drawExtent.Width() + 15u) / 16u;
	uint32_t tilesY = (m_drawExtent.Height() + 15u) / 16u;
	lumaPush.totalLumaTiles = tilesX * tilesY;
	lumaPush.cameraExposure = m_profiler.toneMappingSettings.cameraExposure;

	glm::vec2 halfResSize = {
		static_cast<float>(m_rtRayListLayout.halfWidth),
		static_cast<float>(m_rtRayListLayout.halfHeight)
	};

	glm::vec2 halfResTexel = 1.0f / halfResSize;

	nrdRPush.resSize = halfResSize;
	nrdRPush.resTexel = halfResTexel;
	nrdRPush.writeMotion = 1u;

	// Full screen sizes
	nrdSPush.resSize = glm::vec2(sceneData.viewportSize); // .xy
	nrdSPush.resTexel = fullPixelSize;
	nrdSPush.writeMotion = 0u;

	// RT Shadow push
	{
		rtShadowPush.resolution = nrdSPush.resSize;
		rtShadowPush.invResolution = nrdSPush.resTexel;
	}

	// reflection push
	{
		reflectPush.halfResSize = halfResSize;
		reflectPush.halfResTexel = halfResTexel;
		reflectPush.noiseIndex = noiseIndex;
		reflectPush.rayCapacity = m_rtRayListLayout.capacities[RD::RT_RAY_SLOT_REFLECT];
	}

	if (m_activeEnvSet != debug.activeEnvMap)
	{
		m_activeEnvSet = debug.activeEnvMap;

		const auto& envSet = m_bindlessImageTable.GetEnvironmentSet(m_activeEnvSet);
		forwardPush.specularID = envSet.specular.m_bindlessID;

		reflectPush.specularID = envSet.specular.m_bindlessID;
		reflectPush.skyboxID = envSet.skybox.m_bindlessID;
	}

	forwardPush.reflectRoughCutoff = reflectPush.reflectRoughnessCutoff;
	forwardPush.reflectRoughFade = reflectPush.roughnessFadeStart;
	forwardPush.halfTexel = halfResTexel;

	debug.enableWireframe = m_profiler.enableWireframeView;

	debug.enableFlashlight = LightingSystem::_mainFlashLight.IsFlashLightOn();

	debug.activeInstanceCount = World::GetInstanceState().gpuInputs.size();
	debug.activeLightCount = LightingSystem::GetLightBufferCount();
	debug.activeRTInstances = World::GetInstanceState().rtInstanceCount;
	{
		auto& ssgiPush = m_profiler.ssgiSettings;

		ssgiPush.ndcToViewMul_x_PixelSize = sceneData.ndcToViewMult * fullPixelSize;

		ssgiPush.noiseIndex = noiseIndex;
		ssgiPush.isFinalPass = 0u; // Reset each frame
	}

	m_renderGraphState.UpdateToggles(debug);
	m_renderGraphState.UpdateTemporal(
		frameCtx.IsTemporalValid(),
		frameCtx.IsHiZValid());

	m_renderGraphState.ResetDebugMask();
	// Priority order
	if (debug.enableWireframe)
	{
		m_renderGraphState.SetDebugMask(RD::DebugState::Wireframe);
	}
	else if (frameCtx.m_bDebugLineRendering)
	{
		m_renderGraphState.SetDebugMask(RD::DebugState::OBBLine);
	}
	else if (debug.debugView != 0u)
	{
		m_renderGraphState.SetDebugMask(RD::DebugState::ShadedOverlay);
	}

	DrawPreparation::UploadGPUBuffersForFrame(
		frameCtx,
		m_globalAddressTable,
		m_drawBinTableBuild.binKeys,
		*m_device,
		m_allocator,
		World::GetInstanceState().gpuInputs,
		World::GetInstanceState().rtRows,
		World::GetScene(), // Needs reference
		LightingSystem::_globalLightList,
		frameCtx.IsTemporalValid() && m_renderGraphState.IsTaaOn());

	if (m_renderGraphState.IsNRDActive())
	{
		m_nrdReflectContext.SetFrameSettings(
			sceneData,
			m_bindlessImageTable,
			m_profiler.getStats().deltaSecondsRaw,
			frameCtx.IsTemporalValid() && !IsFirstFrame());

		m_nrdShadowContext.SetFrameSettings(
			sceneData,
			m_bindlessImageTable,
			m_profiler.getStats().deltaSecondsRaw,
			frameCtx.IsTemporalValid() && !IsFirstFrame());
	}

	new (&m_renderPassExecutionContext) RenderPassExecutionContext
	{
		.commandBuffer = frameCtx.GetPrimaryCommandBuffer(), // placeholder
		.frameCtx = &frameCtx,
		.profiler = &m_profiler,
		.imageTable = &m_bindlessImageTable,
		.bufferTable = &m_globalAddressTable,
		.scene = &scene,
		.frameState = &m_renderGraphState,
		.swapchain = &m_swapchain,
		.NRDReflect = &m_nrdReflectContext,
		.NRDShadow = &m_nrdShadowContext,
		.descriptorManager = m_descriptorManager.get(),
		.pipelineManager = m_pipelineManager.get()
	};

	m_renderGraph.SetAsyncComputeEnabled(m_profiler.enableAsyncCompute);

	m_renderGraph.Sync(m_renderGraphState, m_renderPassExecutionContext);

	const auto& schedule = m_renderGraph.GetSchedule();
	auto& async = m_profiler.asyncStats;

	async.bDedicatedQueue    = m_renderGraph.HasDedicatedComputeQueue();
	async.bActiveThisFrame   = schedule.bUsesAsyncCompute;
	async.graphicsBatchCount = schedule.graphicsBatchCount;
	async.asyncPassCount     = static_cast<uint32_t>(schedule.Get(BatchId::C0).passes.size());
	async.overlapPassCount   = static_cast<uint32_t>(schedule.Get(BatchId::G1).passes.size());
}


// =============================================
// Start of the frame
// === CLEAR DATA AND AQUIRE SWAPCHAIN INDEX ===
bool Renderer::PrepareFrame()
{
	auto& frameCtx = GetCurrentFrame();

	if (m_resize.IsPending()) return true;

	// Must always wait first
	auto fenceResult = m_swapchain.WaitOnInFlightFence(frameCtx.m_frameIndex);
	if (fenceResult == VK_ERROR_DEVICE_LOST) m_device->DumpDeviceState("Wait_On_In_Flight_Fence");

	m_device->ResetCrashMarkers(frameCtx.m_frameIndex);

	// Gpu timings
	if (m_device->GetGraphicsQueue().SupportsTimestamps() &&
		frameCtx.m_graphicsTimestampPool != VK_NULL_HANDLE &&
		frameCtx.m_bHasTimestampResultsPending)
	{
		auto results = m_device->GetGraphicsQueue().ReadTimestamps(
			frameCtx.m_graphicsTimestampPool,
			frameCtx.m_passTimestampRanges,
			frameCtx.m_timestampPassUsed,
			m_device->GetTimestampPeriod(),
			true);

		for (uint32_t passIndex = 0; passIndex < TIMESTAMP_PASS_COUNT; ++passIndex)
		{
			if (!results.passResults[passIndex].valid) continue;

			m_profiler.AddGpuPassTime(
				static_cast<RD::Renderer_Pass>(passIndex),
				results.passResults[passIndex].gpuMs);
		}

		if (results.frameResult.valid)
		{
			auto& stats = m_profiler.getStats();
			stats.gpuFrameTimeRawMs = results.frameResult.gpuMs;
			stats.gpuFrameTime.Add(results.frameResult.gpuMs);
		}
	}

	if (m_device->GetComputeQueue().SupportsTimestamps() &&
		frameCtx.m_computeTimestampPool != VK_NULL_HANDLE &&
		frameCtx.m_bHasComputeTimestampsPending.load(std::memory_order_relaxed))
	{
		auto results = m_device->GetComputeQueue().ReadTimestamps(
			frameCtx.m_computeTimestampPool,
			frameCtx.m_passTimestampRanges,
			frameCtx.m_timestampPassUsedCompute,
			m_device->GetTimestampPeriod(),
			false);

		for (uint32_t passIndex = 0; passIndex < TIMESTAMP_PASS_COUNT; ++passIndex)
		{
			if (!results.passResults[passIndex].valid) continue;

			m_profiler.AddGpuPassTime(
				static_cast<RD::Renderer_Pass>(passIndex),
				results.passResults[passIndex].gpuMs);
		}
	}

	if (frameCtx.m_graphicsTimestampPool != VK_NULL_HANDLE)
	{
		vkResetQueryPool(
			m_device->GetContext().device,
			frameCtx.m_graphicsTimestampPool,
			0u,
			TIMESTAMP_QUERY_COUNT);

		frameCtx.m_timestampPassUsed.fill(false);
		frameCtx.m_bHasTimestampResultsPending = false;
	}

	if (frameCtx.m_computeTimestampPool != VK_NULL_HANDLE)
	{
		vkResetQueryPool(
			m_device->GetContext().device,
			frameCtx.m_computeTimestampPool,
			0u,
			TIMESTAMP_QUERY_COUNT);

		frameCtx.m_timestampPassUsedCompute.fill(false);
		frameCtx.m_bHasComputeTimestampsPending.store(false, std::memory_order_relaxed);
	}

	// Next swapchain image
	auto swapResult = m_swapchain.AcquireNextImage(frameCtx.m_frameIndex);
	if (swapResult == VK_ERROR_DEVICE_LOST) m_device->DumpDeviceState("Acquire_Next_Image");

	// This condition should basically never occur
	if (swapResult == VK_ERROR_OUT_OF_DATE_KHR)
	{
		m_resize.Request(ResizeReason::AcquireOutOfDate);
		return true;
	}

	INVARIANT(swapResult == VK_SUCCESS || swapResult == VK_SUBOPTIMAL_KHR);

	// In use swapchain image
	m_swapchain.MarkInFlightFrameIndex(frameCtx.m_frameIndex);

	vmaSetCurrentFrameIndex(m_allocator.GetVma(), static_cast<uint32_t>(m_frameNumber));

	frameCtx.FreeStashedCmds(m_device->GetContext());

	// Primarly uniform buffer cleanup
	frameCtx.m_cpuDeletionQueue.Flush();

	// =================================
	// The safe zone now to do whatever
	// =================================

	const auto& debug = m_profiler.debugToggles;

	const bool wantsDebug =
		debug.showOpaqueOBBs ||
		debug.showTransparentOBBs;

	if (wantsDebug != frameCtx.m_bDebugLineRendering)
	{
		if (wantsDebug)
			frameCtx.CreateDebugBuffers(m_allocator);
		else
			frameCtx.DestroyDebugBuffers(m_allocator);

		frameCtx.m_bDebugLineRendering = wantsDebug;
	}

	if (frameCtx.DoesCachedExtentNeedUpdate(m_drawExtent.Width(), m_drawExtent.Height()))
	{
		frameCtx.CreateClusterBuffers(m_clusterBufferSizes, m_allocator);
		frameCtx.CreateRTRayListBuffer(m_rtRayListLayout, m_allocator);
	}

	frameCtx.SwapMeshletVisibility();

	// Handles initialization and any updates during runtime
	if (frameCtx.m_gpuAddressTable.IsTableDirty())
	{
		frameCtx.m_gpuAddressTable.UpdateCpuVersion();
	}

	if (m_profiler.debugToggles.enableProfilerView && frameCtx.m_statsMapped)
	{
		vmaInvalidateAllocation(m_allocator.GetVma(), frameCtx.m_statsReadback.m_allocation, 0, sizeof(GPUStats));
		m_profiler.gpuStats = *frameCtx.m_statsMapped;
	}

	return m_resize.IsPending();
}

// ===============================================
// === SYNC FRAME SEMAPHORES AND PRESENT FRAME ===
bool Renderer::SubmitFrame()
{
	auto& frameCtx = GetCurrentFrame();

	const uint64_t transferWaitForG1 = frameCtx.transferWaitValue;

	auto& graphicsQ = m_device->GetGraphicsQueue();
	auto& transferQ = m_device->GetTransferQueue();
	auto& computeQ  = m_device->GetComputeQueue();
	auto& presentQ  = m_device->GetPresentQueue();

	VkSemaphore presentSem = m_swapchain.GetAvailableSemaphore();
	VkSemaphore renderSem  = m_swapchain.GetFinishedSemaphore();
	VkFence     fence      = m_swapchain.GetInFlightFence();

	const auto& schedule = m_renderGraph.GetSchedule();

	std::vector<VkSemaphoreSubmitInfo> firstBatchWaits;

	if (frameCtx.transferWaitValue != UINT64_MAX)
	{
		ASSERT(frameCtx.transferWaitValue <= transferQ.GetCurrentSignalValue(),
			"Transfer wait %llu ahead of signalled %llu.",
			frameCtx.transferWaitValue, transferQ.GetCurrentSignalValue());

		firstBatchWaits.emplace_back(TimelineWait(
			transferQ.GetTimelineSemaphore(),
			frameCtx.transferWaitValue,
			VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT));

		frameCtx.transferWaitValue = UINT64_MAX;
	}

	const VkPipelineStageFlags2 kAcquireStages =
		VK_PIPELINE_STAGE_2_BLIT_BIT |
		VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

	if (!schedule.bUsesAsyncCompute)
	{
		// ================= single submit =================
		ASSERT(schedule.graphicsBatchCount == 1u);

		firstBatchWaits.emplace_back(BinaryWait(presentSem, kAcquireStages));

		VK_CHECK(vkResetFences(
			m_device->GetContext().device,
			1,
			&fence));

		auto gfxResult = graphicsQ.SubmitFrame(
			firstBatchWaits,
			frameCtx.GetGraphicsPrimary(0u),
			renderSem,
			fence);
		if (gfxResult == VK_ERROR_DEVICE_LOST) m_device->DumpDeviceState("Graphics_Submit_Single");
	}
	else
	{
		// ============ G0 -> C0 || G1 -> G2 ============
		ASSERT(schedule.graphicsBatchCount == MAX_GRAPHICS_PRIMARIES,
			"Async path expects exactly %u graphics batches, got %u.",
			MAX_GRAPHICS_PRIMARIES, schedule.graphicsBatchCount);

		// ---- G0 ----
		const uint64_t g0 = graphicsQ.AdvanceTimeline();
		{
			const VkSemaphoreSubmitInfo signals[] = {
				TimelineSignal(graphicsQ.GetTimelineSemaphore(), g0)
			};

			graphicsQ.Submit2(
				firstBatchWaits,
				frameCtx.GetGraphicsPrimary(0u),
				signals,
				VK_NULL_HANDLE);
		}

		// ---- C0 ----
		const uint64_t c0 = computeQ.AdvanceTimeline();
		{
			const VkSemaphoreSubmitInfo waits[] = {
				TimelineWait(
					graphicsQ.GetTimelineSemaphore(),
					g0,
					VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)
			};

			const VkSemaphoreSubmitInfo signals[] = {
				TimelineSignal(
					computeQ.GetTimelineSemaphore(),
					c0,
					VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)
			};

			computeQ.Submit2(
				waits,
				frameCtx.GetAsyncComputePrimary(),
				signals,
				VK_NULL_HANDLE);
		}

		// ---- G1 ----
		{
			std::vector<VkSemaphoreSubmitInfo> g1Waits;

			if (transferWaitForG1 != UINT64_MAX)
			{
				g1Waits.emplace_back(TimelineWait(
					transferQ.GetTimelineSemaphore(),
					transferWaitForG1,
					VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT));
			}

			graphicsQ.Submit2(
				g1Waits,
				frameCtx.GetGraphicsPrimary(1u),
				{},
				VK_NULL_HANDLE);
		}

		// ---- G2  ----
		{
			const VkSemaphoreSubmitInfo waits[] = {
				TimelineWait(
					computeQ.GetTimelineSemaphore(), c0,
					VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT),
				BinaryWait(presentSem, kAcquireStages)
			};

			VK_CHECK(vkResetFences(
				m_device->GetContext().device,
				1,
				&fence));

			auto gfxResult = graphicsQ.SubmitFrame(
				std::vector<VkSemaphoreSubmitInfo>(std::begin(waits), std::end(waits)),
				frameCtx.GetGraphicsPrimary(2u),
				renderSem,
				fence);

			if (gfxResult == VK_ERROR_DEVICE_LOST) m_device->DumpDeviceState("Graphics_Submit_Async");
		}
	}

	// ---- present + resize handling: ----
	auto presentResult = presentQ.Present(
		m_swapchain.GetSwapchainHandle(),
		m_swapchain.GetCurrentSwapchainImageIndex(),
		renderSem);
	if (presentResult == VK_ERROR_DEVICE_LOST) m_device->DumpDeviceState("Swapchain_Present");

	if (presentResult == VK_ERROR_OUT_OF_DATE_KHR)
	{
		m_resize.Request(ResizeReason::PresentOutOfDate);
		CheckCSMAtlasExtentUpdate();
		m_frameNumber++;
		return true;
	}

	INVARIANT(presentResult == VK_SUCCESS || presentResult == VK_SUBOPTIMAL_KHR);

	CheckCSMAtlasExtentUpdate();
	m_frameNumber++;
	return m_resize.IsPending();
}

void Renderer::TickVramUsage()
{
	if (m_profiler.debugToggles.enableProfilerView)
	{
		m_profiler.SetVRAMUsage(m_allocator.GetTotalVRAMUsage());
	}
}

bool Renderer::ResolveResize(Extents2D liveExtent)
{
	//RESIZE_TRACE("[Resize] enter phase={} reason={} coalesced={} live={}x{} draw={}x{} gen={}",
	//	ResizeCoordinator::ToString(m_resize.GetPhase()),
	//	ResizeCoordinator::ToString(m_resize.GetReason()),
	//	m_resize.GetCoalesced(),
	//	liveExtent.Width(), liveExtent.Height(),
	//	m_drawExtent.Width(), m_drawExtent.Height(),
	//	m_resize.GetGeneration());

	if (!m_resize.IsPending())
	{
		if (liveExtent.Width() == m_drawExtent.Width() &&
			liveExtent.Height() == m_drawExtent.Height())
			return true;

		RESIZE_TRACE("[Resize] extent mismatch, self-requesting");
		m_resize.Request(ResizeReason::WindowEvent);
	}

	if (!m_resize.CanApply(liveExtent))
	{
		RESIZE_TRACE("[Resize] BLOCKED phase={} live={}x{}",
			ResizeCoordinator::ToString(
				m_resize.GetPhase()), liveExtent.Width(), liveExtent.Height());
		return false;
	}

	m_resize.EnterDrain();
	RESIZE_TRACE("[Resize] drain begin");
	DrainFrameContexts();
	RESIZE_TRACE("[Resize] drain end");

	m_resize.EnterApply();
	RESIZE_TRACE("[Resize] apply begin -> {}x{}", liveExtent.Width(), liveExtent.Height());
	UpdateDrawExtentUsage(liveExtent);
	RESIZE_TRACE("[Resize] extent applied, rebuilding contexts");
	RebuildFrameContexts();
	RESIZE_TRACE("[Resize] rebuild end");

	m_resize.Complete(m_drawExtent);
	RESIZE_TRACE("[Resize] complete gen={} phase={} coalesced={}",
		m_resize.GetGeneration(),
		ResizeCoordinator::ToString(m_resize.GetPhase()),
		m_resize.GetCoalesced());

	ValidateExtentCoherence();
	RESIZE_TRACE("[Resize] validated");

	return true;
}

void Renderer::DrainFrameContexts()
{
	const auto& ctxDevice = m_device->GetContext();

	StallDevice();

	for (uint32_t i = 0; i < m_framesInFlight; ++i)
	{
		auto& frameCtx = m_frameContexts[i];

		frameCtx.FreeStashedCmds(ctxDevice);
		frameCtx.m_cpuDeletionQueue.Flush();

		frameCtx.InvalidateMeshletVisibility();

		frameCtx.ResetDrawExtentCache();

		frameCtx.m_bHasTimestampResultsPending = false;
		frameCtx.m_bHasComputeTimestampsPending.store(false, std::memory_order_relaxed);
		frameCtx.m_timestampPassUsed.fill(false);
		frameCtx.m_timestampPassUsedCompute.fill(false);

		if (frameCtx.m_graphicsTimestampPool != VK_NULL_HANDLE)
			vkResetQueryPool(ctxDevice.device, frameCtx.m_graphicsTimestampPool, 0u, TIMESTAMP_QUERY_COUNT);

		if (frameCtx.m_computeTimestampPool != VK_NULL_HANDLE)
			vkResetQueryPool(ctxDevice.device, frameCtx.m_computeTimestampPool, 0u, TIMESTAMP_QUERY_COUNT);

		frameCtx.transferWaitValue = UINT64_MAX;
	}
}

void Renderer::RebuildFrameContexts()
{
	for (uint32_t i = 0; i < m_framesInFlight; ++i)
	{
		auto& frameCtx = m_frameContexts[i];
		frameCtx.SetTemporalResult(false);
		frameCtx.SetHiZValidResult(false);
	}

	m_renderGraphState.UpdateTemporal(false, false);
}

void Renderer::ValidateExtentCoherence()
{
	const uint32_t w = m_drawExtent.Width();
	const uint32_t h = m_drawExtent.Height();

	INVARIANT(w > 0u && h > 0u);

	if (!m_bindlessImageTable.IsShadowAtlasCached())
	{
		const auto& csmAtlas = m_bindlessImageTable.GetRenderTarget(
			RD::Renderer_RenderTarget::DirectionalCSMAtlas);
		const auto& csmWidth = World::GetScene().GetCSMAtlasWidth();
		const auto& csmHeight = World::GetScene().GetCSMAtlasHeight();

		ASSERT(csmAtlas.Width() == csmWidth,
			"CSM atlas is %u wide, scene believes %u.",
			csmAtlas.Width(), csmHeight);
	}

	ASSERT(m_rtRayListLayout.halfWidth == (w + 1u) / 2u &&
		m_rtRayListLayout.halfHeight == (h + 1u) / 2u,
		"RTRayListLayout %ux%u stale against extent %ux%u.",
		m_rtRayListLayout.halfWidth, m_rtRayListLayout.halfHeight, w, h);

	const auto& opaque = m_bindlessImageTable.GetRenderTarget(RD::Renderer_RenderTarget::Opaque);
	ASSERT(opaque.Width() == w && opaque.Height() == h,
		"Opaque target %ux%u stale against extent %ux%u.",
		opaque.Width(), opaque.Height(), w, h);
}


void Renderer::UpdateDrawExtentUsage(Extents2D newWindowExtent)
{
	SetDrawExtent(newWindowExtent);

	const uint32_t width = m_drawExtent.Width();
	const uint32_t height = m_drawExtent.Height();

	RESIZE_TRACE("RESIZE before swapchain");

	m_swapchain.ResizeSwapchain(
		m_device->GetContext(),
		m_device->GetSurface(),
		m_device->GetSwapchainSupportDetails(),
		newWindowExtent);

	RESIZE_TRACE("RESIZE swapchain complete");

	ASSERT(
		m_swapchain.GetImageCount() == m_framesInFlight,
		"Swapchain image count changed on resize (%u -> %u); frame contexts are not sized for this.",
		m_framesInFlight,
		m_swapchain.GetImageCount());

	m_rtRayListLayout.Update(width, height);
	m_clusterBufferSizes.UpdateClusterBufferSizes(width, height);
	m_renderGraph.SetDrawExtent(m_drawExtent);

	m_bindlessImageTable.UpdateRenderTargets({width, height, 1u}, m_allocator);

	{
		auto cmdGfxPool = m_device->GetThreadCommandPool(JobSystem::RENDER_THREAD, QueueType::Graphics);

		m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
			{
				m_bindlessImageTable.TransitionRenderTargetsFromUndefined(cmd);
			}, cmdGfxPool, QueueType::Graphics);

		m_device->SubmitDeferredCommands(QueueType::Graphics);
		m_device->GetGraphicsQueue().WaitIdle();
	}

	m_nrdReflectContext.Resize(
		m_allocator,
		{ (width + 1u) / 2u, (height + 1u) / 2u });

	m_nrdShadowContext.Resize(
		m_allocator,
		m_drawExtent);

	{
		auto cmdCompPool = m_device->GetThreadCommandPool(JobSystem::RENDER_THREAD, QueueType::Compute);

		m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
			{
				m_nrdReflectContext.RecordPoolInit(cmd);
				m_nrdShadowContext.RecordPoolInit(cmd);
			}, cmdCompPool, QueueType::Compute);

		m_device->SubmitDeferredCommands(QueueType::Compute);
		m_device->GetComputeQueue().WaitIdle();
	}

	m_renderGraph.InvalidateTrackedLayouts();
}

void Renderer::TimestampPoolStart(FrameContext& frameCtx, VkCommandBuffer cmd)
{
	if (!m_device->GetGraphicsQueue().SupportsTimestamps()) return;
	if (frameCtx.m_graphicsTimestampPool == VK_NULL_HANDLE) return;

	vkCmdWriteTimestamp2(
		cmd,
		VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
		frameCtx.m_graphicsTimestampPool,
		FRAME_BEGIN_QUERY);
}

void Renderer::TimestampPoolEnd(FrameContext& frameCtx, VkCommandBuffer cmd)
{
	if (m_device->GetGraphicsQueue().SupportsTimestamps())
	{
		vkCmdWriteTimestamp2(
			cmd,
			VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
			frameCtx.m_graphicsTimestampPool,
			FRAME_END_QUERY);
	}

	m_profiler.CollectTracyGraphics(cmd);

	if (m_device->GetGraphicsQueue().SupportsTimestamps() &&
		frameCtx.m_graphicsTimestampPool != VK_NULL_HANDLE)
	{
		frameCtx.m_bHasTimestampResultsPending = true;
	}
}

void Renderer::BarrierDynamicBuffers(FrameContext& frameCtx, VkCommandBuffer cmd)
{
	auto& addrTable = frameCtx.m_gpuAddressTable;

	if (frameCtx.m_bTransformsBufferUploadNeeded)
	{
		if (frameCtx.IsTemporalValid() && m_renderGraphState.IsTaaOn())
		{
			BufferBarriers::TransferWriteToComputeRead(
				cmd,
				addrTable.GetGPUBuffer(RD::Renderer_Buffer::MotionMatrices),
				m_device->GetContext());
		}

		BufferBarriers::TransferWriteToComputeRead(
			cmd,
			addrTable.GetGPUBuffer(RD::Renderer_Buffer::DynamicTransforms),
			m_device->GetContext());

		frameCtx.ClearTransformsUploadFlag();
	}

	if (frameCtx.m_bLightsBufferUploadNeeded)
	{
		BufferBarriers::TransferWriteToComputeRead(
			cmd,
			addrTable.GetGPUBuffer(RD::Renderer_Buffer::Lights),
			m_device->GetContext());

		frameCtx.ClearLightsUploadFlag();
	}

	if (frameCtx.m_bInstanceInputUploadNeeded)
	{
		BufferBarriers::TransferWriteToComputeRead(
			cmd,
			m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::InstanceInputs),
			m_device->GetContext());

		BufferBarriers::TransferWriteToComputeRead(
			cmd,
			m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::RTRows),
			m_device->GetContext());

		BufferBarriers::TransferWriteToComputeRead(
			cmd,
			m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::DrawBinKeys),
			m_device->GetContext());

		BufferBarriers::TransferWriteToComputeRead(
			cmd,
			m_globalAddressTable.GetTableBuffer(),
			m_device->GetContext());

		frameCtx.ClearInstanceInputUploadFlag();
	}

	if (frameCtx.m_gpuAddressTable.IsTableDirty())
	{
		BufferBarriers::TransferWriteToComputeRead(
			cmd,
			frameCtx.m_gpuAddressTable.GetTableBuffer(),
			m_device->GetContext());

		frameCtx.m_gpuAddressTable.ClearDirty();
	}
}

void Renderer::RecordRenderCommand(JobSystem& jobSystem)
{
	ValidateExtentCoherence();

	auto& frameCtx = GetCurrentFrame();
	auto& frameAddrTable = frameCtx.m_gpuAddressTable;

	CheckGlobalDescriptorSetSync();

	// Catastrophic if version mismatch
	frameAddrTable.IsVersionMismatched();

	// Frame descriptor updates
	frameCtx.TickDescriptorWrites(m_mainWriter);
	m_mainWriter.UpdateSet(m_device->GetContext().device, frameCtx.m_frameSet);

	const bool bBindIndexBuffer =
		m_profiler.assetCounts.totalIndexCount > 0 &&
		m_renderGraphState.InstancesActive() &&
		!World::_loadedScenes.empty();

	RecordHooks hooks;

	m_checkpointPassCounter.store(0, std::memory_order_relaxed);

	hooks.onFrameBegin = [&](VkCommandBuffer cmd)
		{
			m_device->SetCheckpoint(cmd, "Frame_Begin");
			TimestampPoolStart(frameCtx, cmd);
			BarrierDynamicBuffers(frameCtx, cmd);
		};

	hooks.onFrameEnd = [&](VkCommandBuffer cmd)
		{
			TimestampPoolEnd(frameCtx, cmd);
			m_device->SetCheckpoint(cmd, "Frame_End");
		};

	hooks.onAsyncBatchEnd = [&](VkCommandBuffer cmd)
		{
			m_profiler.CollectTracyCompute(cmd);
			m_device->SetCheckpoint(cmd, "Async_Batch_End");
		};

	hooks.bindPrologue = [&](VkCommandBuffer cmd, PassQueue queue)
		{
			const uint32_t idx = m_checkpointPassCounter.fetch_add(1, std::memory_order_relaxed);
			const QueueType qType =
				(queue == PassQueue::Graphics) ? QueueType::Graphics : QueueType::Compute;

			m_device->MarkPassBegin(cmd, qType, frameCtx.m_frameIndex, idx);
			m_device->MarkPassEnd(cmd, qType, frameCtx.m_frameIndex, idx);

			if (queue == PassQueue::Graphics)
			{
				m_descriptorManager->BindDescriptorSetsGraphics(
					cmd,
					frameCtx.m_frameSet,
					m_pipelineManager->GetGlobalLayout());

				m_descriptorManager->BindDescriptorSetsCompute(
					cmd,
					frameCtx.m_frameSet,
					m_pipelineManager->GetGlobalLayout());
			}
			else
			{
				m_descriptorManager->BindDescriptorSetsCompute(
					cmd,
					frameCtx.m_frameSet,
					m_pipelineManager->GetGlobalLayout());
			}

			if (queue == PassQueue::Graphics && bBindIndexBuffer)
			{
				const auto indexBuffer =
					m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::Index).m_buffer;
				vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
			}
		};

	m_renderGraph.RecordFrame(
		m_renderPassExecutionContext,
		jobSystem,
		frameCtx,
		hooks);
}

void Renderer::UpdateShadowMode()
{
	// Physical CSM residency follows the CURRENT requested shadow mode,
	// not the previous frame's RenderStateInfo.
	const bool wantsRTShadows =
		m_profiler.debugToggles.enableShadows != 0u &&
		m_profiler.debugToggles.sunShadowFilter ==
		static_cast<uint32_t>(RD::SunShadowFilter::RT_SOFT);

	const bool csmAtlasCached = m_bindlessImageTable.IsShadowAtlasCached();

	if (wantsRTShadows && !csmAtlasCached)
	{
		StallDevice();

		m_bindlessImageTable.FreeCSMAtlas(m_allocator);

		m_renderGraph.NotifyLayout(
			RD::Renderer_RenderTarget::DirectionalCSMAtlas,
			RD::ImageAccess::Undefined);
	}
	else if (!wantsRTShadows && csmAtlasCached)
	{
		StallDevice();

		m_bindlessImageTable.RecreateCSMAtlas(m_allocator);

		m_renderGraph.NotifyLayout(
			RD::Renderer_RenderTarget::DirectionalCSMAtlas,
			RD::ImageAccess::Undefined);

		const auto& csmAtlas = m_bindlessImageTable.GetRenderTarget(
			RD::Renderer_RenderTarget::DirectionalCSMAtlas);
		World::GetScene().InitCSMInfo(csmAtlas.Width(), csmAtlas.Height(), csmAtlas.m_bindlessID);
	}

	// Now expose the FINAL physical state for this frame.
	m_profiler.debugToggles.csmAtlasCached = m_bindlessImageTable.IsShadowAtlasCached();
}

void Renderer::StallDevice()
{
	const VkResult result = vkDeviceWaitIdle(m_device->GetContext().device);

	if (result == VK_ERROR_DEVICE_LOST) m_device->DumpDeviceState("Stall_Device");

	VK_CHECK(result);
}

void Renderer::FreeAllAssetTextures()
{
	const auto span = m_bindlessImageTable.GetAssetTextureSpan();
	for (uint32_t i = 0; i < static_cast<uint32_t>(span.size()); ++i)
	{
		if (!span[i].IsValid()) continue;
		AllocatedImage& img = m_bindlessImageTable.GetAssetTextureMutable(i);
		m_allocator.FreeImage(img);
		m_bindlessImageTable.FreeAssetTexture(i);
	}
}

void Renderer::UnloadAllScenes()
{
	FreeAllAssetTextures();
	m_registeredMeshes = MeshRegistry{};
	m_materials.clear();
	m_materialFlagsIDs.clear();
	m_blasAddresses.clear();
	m_globalAddressTable.ClearAssetBuffers(m_allocator);
	m_profiler.assetCounts.Clear();

	fmt::println("[Renderer] All scenes unloaded.");
}

void Renderer::Cleanup()
{
	LightingSystem::Cleanup();
	World::Cleanup();

#ifdef TRACY_ENABLE
	m_profiler.ShutdownTracyGPU();
#endif

	DestroyRenderGraph();

	m_nrdReflectContext.Shutdown(m_device->GetContext().device, m_allocator);
	m_nrdShadowContext.Shutdown(m_device->GetContext().device, m_allocator);

	for (auto as : m_blasHandles)
		vkDestroyAccelerationStructureKHR(m_device->GetContext().device, as, nullptr);
	m_blasHandles.clear();
	m_allocator.FreeBuffer(m_blasStorage);

	m_bindlessImageTable.Shutdown(m_device->GetContext().device, m_allocator);
	m_globalAddressTable.Shutdown(m_allocator);

	CleanupFrameResources();

	m_descriptorManager->CleanupDescriptors(m_device->GetContext().device);
	m_pipelineManager->Shutdown(m_device->GetContext().device);

	m_allocator.Shutdown();
	m_swapchain.Cleanup();
	m_device->Cleanup();
}
