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
#include "core/AssetUploadTypes.h"

static_assert(sizeof(InstanceInput)   == SIZEOF_INSTANCE_INPUT);
static_assert(sizeof(DrawBin)         == SIZEOF_DRAW_BIN);

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
		RD::AntiAliasingMethod::AA_CMAA2,
		RD::AmbientOcclusionMethod::AO_GTAO_BENT_NORMALS,
		RD::ShadowQuality::High,
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
	auto mainThreadPool = m_device->GetThreadCommandPool(mainThread.threadID, QueueType::Graphics);
	m_profiler.SetTracyGraphicsCmd(m_device->CreateCommandBuffer(mainThreadPool));

	m_profiler.InitTracyGPU(
		m_device->GetContext().physicalDevice,
		m_device->GetContext().device,
		m_device->GetGraphicsQueue().GetQueue(),
		m_profiler.GetTracyGraphicsCmd());
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
	InitFrameResources();

	const size_t totalFrameStaging =
		GPU_BYTES_INSTANCE_INPUT +
		GPU_BYTES_DRAW_BIN_KEYS + 
		GPU_BYTES_TRANSFORMS +
		GPU_BYTES_LIGHTS +
		m_globalAddressTable.GPU_ADDRESS_TABLE_SIZE_GPU_BYTES;

	m_allocator.InitFrameStaging(totalFrameStaging, m_device->GetNonCoherentAtomSize());

	// =============================
	// === Global resource setup ===

	// --------
	// Buffers
	//---------

	m_globalAddressTable.Init(m_allocator);

	m_globalAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::Luminance,
		GPU_BYTES_LUMINANCE,
		m_allocator);

	m_globalAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::InstanceInputs,
		GPU_BYTES_INSTANCE_INPUT,
		m_allocator);

	m_globalAddressTable.AddGPUBufferToAddressTable(
		RD::Renderer_Buffer::DrawBinKeys,
		GPU_BYTES_DRAW_BIN_KEYS,
		m_allocator);


	m_clusterBufferSizes.UpdateClusterBufferSizes(m_drawExtent.Width(), m_drawExtent.Height());
	m_cmaa2BufferSizes.UpdateCmaa2BufferSizes(m_drawExtent.Width(), m_drawExtent.Height());

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

	// ==========================
	// === Environment setup ====

	jobSystem.SubmitJob([&](ThreadContext& threadCtx) {
		auto cmdpool = m_device->GetThreadCommandPool(threadCtx.threadID, QueueType::Graphics);

		std::vector<PipelineHandle> envPipelines = {
			m_pipelineManager->GetHandle(RD::Renderer_Pipeline::HDRToCubemap),
			m_pipelineManager->GetHandle(RD::Renderer_Pipeline::DiffuseIrradiance),
			m_pipelineManager->GetHandle(RD::Renderer_Pipeline::SpecularPrefilter),
			m_pipelineManager->GetHandle(RD::Renderer_Pipeline::BRDFLUT) // Not apart of env set
		};
		m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
			{
				BakeEnvironmentMaps(
					cmd,
					m_bindlessImageTable,
					envPipelines);
			}, cmdpool, QueueType::Graphics);
	});

	// Global address table and luminance buffer upload
	jobSystem.SubmitJob([&](ThreadContext& threadCtx) {
		auto cmdpool = m_device->GetThreadCommandPool(threadCtx.threadID, QueueType::Transfer);

		auto stageCopyLuminance = m_allocator.GlobalStaging.Stage(
			m_luminanceSums,
			GPU_BYTES_LUMINANCE,
			m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::Luminance).m_buffer);

		auto stageCopyGlobalAddrTable = m_allocator.GlobalStaging.Stage(
			m_globalAddressTable.GetAddrPtrTable().data(),
			m_globalAddressTable.GPU_ADDRESS_TABLE_SIZE_GPU_BYTES,
			m_globalAddressTable.GetTableBuffer().m_buffer);

		m_allocator.GlobalStaging.Flush();

		m_device->RecordDeferredCommand([&, stageCopyLuminance, stageCopyGlobalAddrTable](VkCommandBuffer cmd)
		{
			m_allocator.GlobalStaging.CopyCommand(cmd, stageCopyLuminance);
			m_allocator.GlobalStaging.CopyCommand(cmd, stageCopyGlobalAddrTable);
		}, cmdpool, QueueType::Transfer);
	});

	jobSystem.Wait();

	m_device->SubmitDeferredCommands(QueueType::Graphics);
	m_device->SubmitDeferredCommands(QueueType::Transfer);

	m_allocator.GlobalStaging.Reset();

	// ===============================
	// === Global descriptor setup ===

	jobSystem.SubmitJob([&](ThreadContext& threadCtx) {
		m_bindlessImageTable.BuildInitialCombinedSamplerArray();
	});

	jobSystem.SubmitJob([&](ThreadContext& threadCtx) {
		m_bindlessImageTable.BuildInitialSamplerCubeArray();
	});

	jobSystem.Wait();

	{
		auto cmdPool = m_device->GetThreadCommandPool(JobSystem::RENDER_THREAD, QueueType::Graphics);

		m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
		{
			m_bindlessImageTable.TransitionRenderTargetsFromUndefined(cmd);
		}, cmdPool, QueueType::Graphics);

		m_device->SubmitDeferredCommands(QueueType::Graphics);
		m_device->GetGraphicsQueue().WaitIdle();
	}

	m_bindlessImageTable.FreeEquirects(m_allocator);

	m_profiler.SetVRAMUsage(m_allocator.GetTotalVRAMUsage());

	CreateRenderGraph();

	World::Init(m_bindlessImageTable);

	auto& smaaPush = m_profiler.smaaTexturesIds;
	smaaPush.id0 = m_bindlessImageTable.GetStaticTexture(RD::Renderer_Texture::SMAASearch).m_bindlessID;
	smaaPush.id1 = m_bindlessImageTable.GetStaticTexture(RD::Renderer_Texture::SMAAArea).m_bindlessID;

	auto& forwardPush = m_profiler.forwardPush;
	forwardPush.flashlightCookieTexID = LightingSystem::_mainFlashLight.m_cookieGoboID;
	forwardPush.flashlightShadowMapID = LightingSystem::_mainFlashLight.m_shadowMapID;
	forwardPush.brdfID = m_bindlessImageTable.GetStaticTexture(RD::Renderer_Texture::Brdf).m_bindlessID;

	m_profiler.lensFlareSettings.rainbowLUTIndex = m_bindlessImageTable.GetStaticTexture(RD::Renderer_Texture::RainbowLut).m_bindlessID;

	m_profiler.ssaoSettings.hilbertLutID = m_bindlessImageTable.GetStaticTexture(RD::Renderer_Texture::HilbertCurveLut).m_bindlessID;
}

// Only called after swapchain presented and queue wait
void Renderer::CheckCSMAtlasExtentUpdate()
{
	if (m_currentShadowQuality != m_profiler.shadowQuality)
	{
		StallDevice();
		m_currentShadowQuality = m_profiler.shadowQuality;
		m_bindlessImageTable.UpdateCSMAtlasExtent(m_currentShadowQuality, m_allocator);
		const auto& csmAtlas = m_bindlessImageTable.GetRenderTarget(RD::Renderer_RenderTarget::DirectionalCSMAtlas);
		World::GetScene().InitCSMInfo(csmAtlas.Width(), csmAtlas.Height(), csmAtlas.m_bindlessID);
	}
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

void Renderer::InitFrameResources()
{
	m_framesInFlight = m_swapchain.GetImageCount();

	fmt::println("Frames in flight:[{}]", m_framesInFlight);

	for (uint32_t i = 0; i < m_framesInFlight; ++i)
	{
		m_frameContexts[i].Init(
			i,
			m_drawExtent,
			*m_device,
			*m_descriptorManager,
			m_allocator);
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
	m_renderGraph.Build(*m_pipelineManager, m_drawExtent);
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
	RD::AntiAliasingMethod aaMode,
	RD::AmbientOcclusionMethod aoMode,
	RD::ShadowQuality shadowQuality,
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

	toggles.bloomIntensity = 0.06;

	toggles.aaMode = static_cast<uint32_t>(aaMode);
	toggles.aoMode = static_cast<uint32_t>(aoMode);

	toggles.enableProfilerView = enableProfilerView ? 1u : 0u;
	toggles.enableSettings     = enableSettings     ? 1u : 0u;

	m_currentShadowQuality = shadowQuality;
	m_profiler.shadowQuality = shadowQuality;
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
		asset->virtualInstance = batch.virtualInstance;
		assets.push_back(asset);
	}

	// ---- Compute total staging size needed ----
	size_t totalTexBytes  = 0;
	size_t totalVtxBytes  = 0;
	size_t totalIdxBytes  = 0;
	size_t totalMeshBytes = 0;
	size_t totalMatBytes  = 0;

	auto& assetCounts = m_profiler.assetCounts;

	for (auto& batch : batches)
	{
		for (const auto& t : batch.textures)
			if (t.IsValid())
				totalTexBytes += AllocatedBuffer::AlignUp(
					static_cast<size_t>(t.width * t.height) * t.PixelBytes(), 4u);

		// TODO: Eventually add a way to subtract from this initial count
		assetCounts.totalVertexCount += static_cast<uint32_t>(batch.vertices.size());
		assetCounts.totalIndexCount += static_cast<uint32_t>(batch.indices.size());
		assetCounts.totalMeshCount += static_cast<uint32_t>(batch.meshes.size());
		assetCounts.totalMaterialCount += static_cast<uint32_t>(batch.materials.size());

		totalVtxBytes  += batch.vertices.size()  * sizeof(Vertex);
		totalIdxBytes  += batch.indices.size()   * sizeof(uint32_t);
		totalMeshBytes += batch.meshes.size()    * sizeof(Mesh);
		totalMatBytes  += batch.materials.size() * sizeof(Material);
	}

	const size_t totalNeeded = totalTexBytes + totalVtxBytes + totalIdxBytes
							 + totalMeshBytes + totalMatBytes
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

		// Allocate singular global buffers sized for ALL scenes combined
		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::Vertex, totalVtxBytes, m_allocator);

		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::Index, totalIdxBytes, m_allocator);

		m_globalAddressTable.AddGPUBufferToAddressTable(
			RD::Renderer_Buffer::Mesh, totalMeshBytes, m_allocator);

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
			UpdateGlobalBufferTable(cmd);
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
	const auto& vtxBuf  = m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::Vertex);
	const auto& idxBuf  = m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::Index);
	const auto& meshBuf = m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::Mesh);

	// Running cursors — each scene appends after the previous
	uint32_t globalVertexCursor = 0;
	uint32_t globalIndexCursor  = 0;
	uint32_t globalMeshCursor   = 0;

	// Staging offsets into the single global buffer
	size_t stagingVtxOffset  = 0;
	size_t stagingIdxOffset  = 0;
	size_t stagingMeshOffset = 0;

	// Collect all GPU mesh structs into one flat array
	std::vector<Mesh> allGpuMeshes;

	for (size_t b = 0; b < batches.size(); ++b)
	{
		auto& batch = batches[b];
		auto& asset = *assets[b];

		if (batch.meshes.empty()) continue;

		const uint32_t localMeshBase   = globalMeshCursor;
		const uint32_t localVertBase   = globalVertexCursor;
		const uint32_t localIndexBase  = globalIndexCursor;

		asset.meshGlobalIDs.reserve(batch.meshes.size());

		for (auto& md : batch.meshes)
		{
			// Adjust to global offsets
			md.firstIndex       += localIndexBase;
			md.vertexOffset     += localVertBase;
			md.shadowFirstIndex += localIndexBase;

			Mesh gpuMesh{};
			gpuMesh.firstIndex          = md.firstIndex;
			gpuMesh.indexCount          = md.indexCount;
			gpuMesh.vertexOffset        = md.vertexOffset;
			gpuMesh.vertexCount         = md.vertexCount;
			gpuMesh.shadowFirstIndex    = md.shadowFirstIndex;
			gpuMesh.shadowIndexCount    = md.shadowIndexCount;
			gpuMesh.localAABB           = md.localAABB;
			gpuMesh.localBoundingRadius = md.localBoundingRadius;

			const uint32_t globalID = m_registeredMeshes.RegisterMesh(gpuMesh);
			md.globalMeshID = globalID;
			asset.meshGlobalIDs.push_back(globalID);
			allGpuMeshes.push_back(gpuMesh);
		}

		// Wire up LODs using global IDs
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

		globalVertexCursor += static_cast<uint32_t>(batch.vertices.size());
		globalIndexCursor  += static_cast<uint32_t>(batch.indices.size());
		globalMeshCursor   += static_cast<uint32_t>(batch.meshes.size());
	}

	// Stage all scenes into the global buffers in one contiguous write per buffer
	size_t vtxOff = 0, idxOff = 0;

	for (auto& batch : batches)
	{
		if (batch.vertices.empty()) continue;

		const size_t vBytes = batch.vertices.size() * sizeof(Vertex);
		const size_t iBytes = batch.indices.size()  * sizeof(uint32_t);

		// Stage directly into the correct offset of the global GPU buffer
		auto vtxWrite = m_allocator.GlobalStaging.Stage(
			batch.vertices.data(), vBytes, vtxBuf.m_buffer, vtxOff);
		auto idxWrite = m_allocator.GlobalStaging.Stage(
			batch.indices.data(), iBytes, idxBuf.m_buffer, idxOff);

		m_allocator.GlobalStaging.CopyCommand(cmd, vtxWrite);
		m_allocator.GlobalStaging.CopyCommand(cmd, idxWrite);

		vtxOff += vBytes;
		idxOff += iBytes;

		batch.vertices.clear(); batch.vertices.shrink_to_fit();
		batch.indices.clear();  batch.indices.shrink_to_fit();
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

		auto resolve = [&](uint32_t localIdx, RD::Renderer_Texture fallback) -> uint32_t
		{
			if (localIdx == UINT32_MAX ||
				localIdx >= static_cast<uint32_t>(asset.textureBindlessIDs.size()))
				return m_bindlessImageTable.GetStaticTexture(fallback).m_bindlessID;
			return asset.textureBindlessIDs[localIdx];
		};

		for (auto& desc : batch.materials)
		{
			Material mat{};
			mat.albedoID         = resolve(desc.albedoTexIdx,    RD::Renderer_Texture::White);
			mat.metalRoughnessID = resolve(desc.metalRoughTexIdx,RD::Renderer_Texture::MetalRough);
			mat.normalID         = resolve(desc.normalTexIdx,    RD::Renderer_Texture::Normal);
			mat.emissiveID       = resolve(desc.emissiveTexIdx,  RD::Renderer_Texture::Emissive);
			mat.colorFactor      = desc.colorFactor;
			mat.metalRoughFactors= desc.metalRoughFactors;
			mat.emissiveColor    = desc.emissiveColor;
			mat.emissiveStrength = desc.emissiveStrength;
			mat.alphaCutoff      = desc.alphaCutoff;
			mat.normalScale      = desc.normalScale;

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

void Renderer::UpdateGlobalBufferTable(VkCommandBuffer cmd)
{
	auto write = m_allocator.GlobalStaging.Stage(
		m_globalAddressTable.GetAddrPtrTable().data(),
		m_globalAddressTable.GPU_ADDRESS_TABLE_SIZE_GPU_BYTES,
		m_globalAddressTable.GetTableBuffer().m_buffer);

	m_allocator.GlobalStaging.Flush();
	m_allocator.GlobalStaging.CopyCommand(cmd, write);
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

	World::UpdateWorldState(frameCtx, m_allocator, m_profiler, window);

	frameCtx.SetTemporalResult(scene.GetTemporalResult() && !IsFirstFrame());
	frameCtx.SetHiZValidResult(scene.GetSceneData().temporal.z);

	bool instanceUploadNeeded = false;
	instanceUploadNeeded = DrawPreparation::SyncInstanceInputs(
		World::GetInstanceState(),
		scene.GetVirtualInstances(),
		World::_loadedScenes,
		m_registeredMeshes.GetMeshes(),
		m_registeredMeshes.GetLods(),
		scene.GetTransforms(),
		m_materialFlagsIDs);

	frameCtx.FlagInstanceInputUpload(instanceUploadNeeded);

	if (frameCtx.IsInstanceInputsUploadNeeded())
	{
		m_drawBinTableBuild = DrawPreparation::BuildDrawBinTable(World::GetInstanceState().gpuInputs);
	}

	DrawPreparation::UploadGPUBuffersForFrame(
		frameCtx,
		m_globalAddressTable,
		m_drawBinTableBuild.binKeys,
		*m_device,
		m_allocator,
		World::GetInstanceState().gpuInputs,
		scene.GetTransforms(),
		LightingSystem::_globalLightList,
		frameCtx.IsTemporalValid());

	// Updates inline uniform flashtoggle for instance culling shader
	m_profiler.debugToggles.enableFlashlight = LightingSystem::_mainFlashLight.IsFlashLightOn();

	auto& forwardPush = m_profiler.forwardPush;
	auto& lumaPush = m_profiler.lumaExposureSettings;
	auto& taaPush = m_profiler.taaSettings;

	const float rawDt = std::max(float(m_profiler.getStats().deltaSecondsRaw), 1e-5f);
	taaPush.invDeltaTime = 1.0f / rawDt;
	taaPush.deltaTime    = std::clamp(rawDt, 1.0f / RD::TARGET_FPS_240, 1.0f / 30.0f);

	uint32_t tilesX = m_drawExtent.Width() / 16u;
	uint32_t tilesY = m_drawExtent.Height() / 16u;
	lumaPush.totalLumaTiles = tilesX * tilesY;
	lumaPush.cameraExposure = m_profiler.toneMappingSettings.cameraExposure;

	if (m_activeEnvSet != debug.activeEnvMap)
	{
		m_activeEnvSet = debug.activeEnvMap;

		const auto& envSet = m_bindlessImageTable.GetEnvironmentSet(m_activeEnvSet);
		forwardPush.diffuseID = envSet.irradiance.m_bindlessID;
		forwardPush.specularID = envSet.specular.m_bindlessID;
	}

	bool postAACopyNeeded =
		(debug.aaMode != static_cast<uint32_t>(RD::AntiAliasingMethod::AA_OFF) &&
		debug.aaMode != static_cast<uint32_t>(RD::AntiAliasingMethod::AA_TAA));

	m_renderGraphState.bHiZValid = frameCtx.IsHiZValid();
	m_renderGraphState.bTemporalValid = frameCtx.IsTemporalValid();
	m_renderGraphState.bCopyPostAAImage = postAACopyNeeded;
	m_renderGraphState.bShowImgui = ShouldRenderImgui();
	m_renderGraphState.bDebugLine = frameCtx.m_bDebugLineRendering
		&& (debug.showOpaqueOBBs || debug.showTransparentOBBs);

	m_renderGraphState.activeInstanceCount = World::GetInstanceState().gpuInputs.size();

	// These two are implied together
	m_renderGraphState.activeLightCount = LightingSystem::GetActiveLightCount();
	m_renderGraphState.bFlashlightOn = LightingSystem::_mainFlashLight.IsFlashLightOn();

	debug.activeInstanceCount = m_renderGraphState.activeInstanceCount;
	debug.activeLightCount = m_renderGraphState.activeLightCount;

	new (&m_renderPassExecutionContext) RenderPassExecutionContext
	{
		.commandBuffer = frameCtx.m_commandBuffer,
		.frameCtx      = &frameCtx,
		.profiler      = &m_profiler,
		.imageTable    = &m_bindlessImageTable,
		.bufferTable   = &m_globalAddressTable,
		.scene         = &scene,
		.frameState    = &m_renderGraphState,
		.swapchain     = &m_swapchain
	};

	m_renderGraph.Sync(m_renderGraphState);
}


// =============================================
// // Start of the frame
// === CLEAR DATA AND AQUIRE SWAPCHAIN INDEX ===
bool Renderer::PrepareFrame()
{
	auto& frameCtx = GetCurrentFrame();

	// Must always wait first
	m_swapchain.WaitOnInFlightFence(frameCtx.m_frameIndex);

	// Gpu timings
	if (m_device->GetGraphicsQueue().SupportsTimestamps() &&
		frameCtx.m_graphicsTimestampPool != VK_NULL_HANDLE &&
		frameCtx.m_bHasTimestampResultsPending)
	{
		auto results = m_device->GetGraphicsQueue().ReadTimestamps(
			frameCtx.m_graphicsTimestampPool,
			frameCtx.m_passTimestampRanges,
			frameCtx.m_timestampPassUsed,
			m_device->GetTimestampPeriod());

		for (uint32_t passIndex = 0; passIndex < TIMESTAMP_PASS_COUNT; ++passIndex)
		{
			if (!results.passResults[passIndex].valid) continue;

			m_profiler.AddGpuPassTime(
				static_cast<RD::Renderer_Pass>(passIndex),
				results.passResults[passIndex].gpuMs);

			frameCtx.m_timestampPassUsed[passIndex] = false;
		}

		if (results.frameResult.valid)
		{
			auto& stats = m_profiler.getStats();

			stats.gpuFrameTimeRawMs = results.frameResult.gpuMs;
			stats.gpuFrameTime.Add(results.frameResult.gpuMs);
		}

		frameCtx.m_bHasTimestampResultsPending = !results.allReady;
	}

	// Next swapchain image
	auto swapResult = m_swapchain.AcquireNextImage(frameCtx.m_frameIndex);

	// This condition basically never occurs
	if (swapResult == VK_ERROR_OUT_OF_DATE_KHR || swapResult == VK_SUBOPTIMAL_KHR)
	{
		m_device->GetGraphicsQueue().WaitIdle();
		m_bHasDrawExtentResized = true;
		return m_bHasDrawExtentResized;
	}
	INVARIANT(swapResult == VK_SUCCESS);

	// In use swapchain image
	m_swapchain.MarkInFlightFrameIndex(frameCtx.m_frameIndex);

	VK_CHECK(vkResetCommandBuffer(frameCtx.m_commandBuffer, 0));

	frameCtx.FreeStashedCmds(m_device->GetContext());

	// Primarly uniform buffer cleanup
	frameCtx.m_cpuDeletionQueue.Flush();

	// =================================
	// The safe zone now to do whatever
	// =================================

	const auto curWidth = m_drawExtent.Width();
	const auto curHeight = m_drawExtent.Height();

	// Only compute buffer sizes once to share with frames contexts
	if (m_bHasDrawExtentResized)
	{
		m_clusterBufferSizes.UpdateClusterBufferSizes(curWidth, curHeight);
		m_cmaa2BufferSizes.UpdateCmaa2BufferSizes(curWidth, curHeight);

		m_renderGraph.SetDrawExtent(m_drawExtent);

		m_bHasDrawExtentResized = false;
	}

	// Gpu driven buffers, modify each context at a time
	if (frameCtx.DoesCachedExtentNeedUpdate(curWidth, curHeight))
	{
		frameCtx.CreateClusterBuffers(
			m_clusterBufferSizes,
			m_allocator);
		frameCtx.CreateCMAA2Buffers(
			m_cmaa2BufferSizes,
			m_allocator);
	}

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

	return m_bHasDrawExtentResized; // Should be false
}

// ===============================================
// === SYNC FRAME SEMAPHORES AND PRESENT FRAME ===
bool Renderer::SubmitFrame()
{
	auto& frameCtx = GetCurrentFrame();

	auto& graphicsQ = m_device->GetGraphicsQueue();
	auto& transferQ = m_device->GetTransferQueue();
	auto& presentQ = m_device->GetPresentQueue();

	VkSemaphore presentSem = m_swapchain.GetAvailableSemaphore();
	VkSemaphore renderSem  = m_swapchain.GetFinishedSemaphore();
	VkFence     fence      = m_swapchain.GetInFlightFence();

	std::vector<VkSemaphoreSubmitInfo> waitInfos;

	// Wait on image acquired semaphore
	waitInfos.emplace_back(VkSemaphoreSubmitInfo{
		.sType =  VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
		.semaphore = presentSem,
		.stageMask = VK_PIPELINE_STAGE_2_BLIT_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT
		});

	const auto curTransferSignal = transferQ.GetCurrentSignalValue();

	// Wait on transfer timeline only up to the first buffer consumers
	if (frameCtx.transferWaitValue != UINT64_MAX && frameCtx.transferWaitValue <= curTransferSignal)
	{
		INVARIANT(frameCtx.transferWaitValue <= curTransferSignal);
		waitInfos.emplace_back(VkSemaphoreSubmitInfo{
			.sType     = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.semaphore = transferQ.GetTimelineSemaphore(),
			.value     = frameCtx.transferWaitValue,
			.stageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT // Buffers needed during visibility stage
			});
	}

	graphicsQ.SubmitFrame(
		waitInfos,
		frameCtx.m_commandBuffer,
		renderSem,
		fence);

	// Present using the image-indexed renderSem
	auto swapResult = presentQ.Present(
		m_swapchain.GetSwapchainHandle(),
		m_swapchain.GetCurrentSwapchainImageIndex(),
		renderSem);

	if (swapResult == VK_ERROR_OUT_OF_DATE_KHR || swapResult == VK_SUBOPTIMAL_KHR)
	{
		if (graphicsQ.GetQueue() != presentQ.GetQueue())
			presentQ.WaitIdle();
		else
			graphicsQ.WaitIdle();

		m_bHasDrawExtentResized = true;
		CheckCSMAtlasExtentUpdate();

		// Resize swapchain, update window, recreate render targets with new extent
		return m_bHasDrawExtentResized;
	}
	else
	{
		INVARIANT(swapResult == VK_SUCCESS);
	}

	CheckCSMAtlasExtentUpdate();
	m_frameNumber++;
	return m_bHasDrawExtentResized; // Should be false
}

void Renderer::TickVramUsage()
{
	if (m_profiler.getStats().vramQueryTimerSeconds >= 5.0f)
	{
		m_profiler.getStats().vramQueryTimerSeconds -= 5.0f;
		m_profiler.SetVRAMUsage(m_allocator.GetTotalVRAMUsage());
	}
}

void Renderer::UpdateDrawExtentUsage(Extents2D newWindowExtent)
{
	SetDrawExtent(newWindowExtent);

	m_swapchain.ResizeSwapchain(
		m_device->GetContext(),
		m_device->GetSurface(),
		m_device->GetSwapchainSupportDetails(),
		newWindowExtent);

	m_bindlessImageTable.UpdateRenderTargets({ m_drawExtent.Width(), m_drawExtent.Height(), 1u }, m_allocator);

	{
		auto cmdPool = m_device->GetThreadCommandPool(JobSystem::RENDER_THREAD, QueueType::Graphics);

		m_device->RecordDeferredCommand([&](VkCommandBuffer cmd)
		{
			m_bindlessImageTable.TransitionRenderTargetsFromUndefined(cmd);
		}, cmdPool, QueueType::Graphics);

		m_device->SubmitDeferredCommands(QueueType::Graphics);
		m_device->GetGraphicsQueue().WaitIdle();
	}
}

void Renderer::TimestampPoolStart(FrameContext& frameCtx)
{
	if (m_device->GetGraphicsQueue().SupportsTimestamps() && frameCtx.m_graphicsTimestampPool != VK_NULL_HANDLE)
	{
		if (!frameCtx.m_bHasTimestampResultsPending)
		{
			vkCmdResetQueryPool(
				frameCtx.m_commandBuffer,
				frameCtx.m_graphicsTimestampPool,
				0u,
				TIMESTAMP_QUERY_COUNT);

			frameCtx.m_timestampPassUsed.fill(false);
		}
	}
	if (m_device->GetGraphicsQueue().SupportsTimestamps())
	{
		vkCmdWriteTimestamp2(
			frameCtx.m_commandBuffer,
			VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
			frameCtx.m_graphicsTimestampPool,
			FRAME_BEGIN_QUERY);
	}
}

void Renderer::TimestampPoolEnd(FrameContext& frameCtx)
{
	if (m_device->GetGraphicsQueue().SupportsTimestamps())
	{
		vkCmdWriteTimestamp2(
			frameCtx.m_commandBuffer,
			VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
			frameCtx.m_graphicsTimestampPool,
			FRAME_END_QUERY
		);
	}

	m_profiler.CollectTracyGPU(frameCtx.m_commandBuffer);

	if (m_device->GetGraphicsQueue().SupportsTimestamps() && frameCtx.m_graphicsTimestampPool != VK_NULL_HANDLE)
	{
		frameCtx.m_bHasTimestampResultsPending = true;
	}
}

void Renderer::BarrierDynamicBuffers(FrameContext& frameCtx)
{
	auto& addrTable = frameCtx.m_gpuAddressTable;

	if (frameCtx.m_bTransformsBufferUploadNeeded)
	{
		if (frameCtx.IsTemporalValid())
		{
			BufferBarriers::TransferWriteToComputeRead(
				frameCtx.m_commandBuffer,
				addrTable.GetGPUBuffer(RD::Renderer_Buffer::PrevTransforms),
				m_device->GetContext());
		}

		BufferBarriers::TransferWriteToComputeRead(
			frameCtx.m_commandBuffer,
			addrTable.GetGPUBuffer(RD::Renderer_Buffer::Transforms),
			m_device->GetContext());

		frameCtx.ClearTransformsUploadFlag();
	}

	if (frameCtx.m_bLightsBufferUploadNeeded)
	{
		BufferBarriers::TransferWriteToComputeRead(
			frameCtx.m_commandBuffer,
			addrTable.GetGPUBuffer(RD::Renderer_Buffer::Lights),
			m_device->GetContext());

		frameCtx.ClearLightsUploadFlag();
	}

	if (frameCtx.m_bInstanceInputUploadNeeded)
	{
		BufferBarriers::TransferWriteToComputeRead(
			frameCtx.m_commandBuffer,
			m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::InstanceInputs),
			m_device->GetContext());

		BufferBarriers::TransferWriteToComputeRead(
			frameCtx.m_commandBuffer,
			m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::DrawBinKeys),
			m_device->GetContext());

		BufferBarriers::TransferWriteToComputeRead(
			frameCtx.m_commandBuffer,
			m_globalAddressTable.GetTableBuffer(),
			m_device->GetContext());

		frameCtx.ClearInstanceInputUploadFlag();
	}

	if (frameCtx.m_gpuAddressTable.IsTableDirty())
	{
		BufferBarriers::TransferWriteToComputeRead(
			frameCtx.m_commandBuffer,
			frameCtx.m_gpuAddressTable.GetTableBuffer(),
			m_device->GetContext());

		frameCtx.m_gpuAddressTable.ClearDirty();
	}
}

// All recorded in a single graphics queue
void Renderer::RecordRenderCommand()
{
	auto& frameCtx = GetCurrentFrame();

	auto& frameAddrTable = frameCtx.m_gpuAddressTable;

	CheckGlobalDescriptorSetSync();

	// Catastrophic if version mismatch
	frameAddrTable.IsVersionMismatched();

	// Frame descriptor updates
	frameCtx.TickDescriptorWrites(m_mainWriter);
	m_mainWriter.UpdateSet(m_device->GetContext().device, frameCtx.m_frameSet);

	VkCommandBufferBeginInfo cmdBeginInfo{};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK(vkBeginCommandBuffer(frameCtx.m_commandBuffer, &cmdBeginInfo));

	TimestampPoolStart(frameCtx);

	BarrierDynamicBuffers(frameCtx);

	// All data needed for frame is ready
	m_descriptorManager->BindDescriptorSets(
		frameCtx.m_commandBuffer,
		frameCtx.m_frameSet,
		m_pipelineManager->GetGlobalLayout());

	if (m_profiler.assetCounts.totalIndexCount > 0 &&
		m_renderGraphState.activeInstanceCount > 0 &&
		!World::_loadedScenes.empty())
	{
		const auto indexBuffer = m_globalAddressTable.GetGPUBuffer(RD::Renderer_Buffer::Index).m_buffer;
		vkCmdBindIndexBuffer(frameCtx.m_commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
	}

	m_renderGraph.ExecutePasses(m_renderPassExecutionContext);

	TimestampPoolEnd(frameCtx);

	VK_CHECK(vkEndCommandBuffer(frameCtx.m_commandBuffer));
}

void Renderer::StallDevice()
{
	m_device->IdleDevice();
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
	// --- Textures ---
	FreeAllAssetTextures();

	// --- Mesh registry ---
	m_registeredMeshes = MeshRegistry{};

	// --- Materials ---
	m_materials.clear();
	m_materialFlagsIDs.clear();

	// --- GPU buffers in global address table ---
	// BDATable::Shutdown handles all slots already,
	// but these are asset-lifetime buffers we want to clear now
	// without shutting down the whole table
	auto clearIfExists = [&](RD::Renderer_Buffer slot)
	{
		if (m_globalAddressTable.ContainsGPUBuffer(slot))
			m_globalAddressTable.ClearGPUAddressBuffer(slot, m_allocator);
	};

	clearIfExists(RD::Renderer_Buffer::Vertex);
	clearIfExists(RD::Renderer_Buffer::Index);
	clearIfExists(RD::Renderer_Buffer::Mesh);
	clearIfExists(RD::Renderer_Buffer::Material);

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

	m_bindlessImageTable.Shutdown(m_device->GetContext().device, m_allocator);
	m_globalAddressTable.Shutdown(m_allocator);

	CleanupFrameResources();

	m_descriptorManager->CleanupDescriptors(m_device->GetContext().device);
	m_pipelineManager->Shutdown(m_device->GetContext().device);

	m_allocator.Shutdown();
	m_swapchain.Cleanup();
	m_device->Cleanup();
}
