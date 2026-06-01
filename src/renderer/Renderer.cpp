#include "pch.h"

#include "Renderer.h"
#include "backend/memory/Budgets.h"
#include "backend/Device.h"
#include "backend/PipelineManager.h"
#include "backend/DescriptorManager.h"
#include "backend/PhysicalDeviceSelector.h"
#include "backend/BufferBarriers.h"
#include "scene/World.h"
#include "scene/LightingSystem.h"
#include "scene/Scene.h"
#include "core/Window.h"
#include "core/JobSystem.h"
#include "core/Environment.h"
#include "rendergraph/RenderPasses.h"

// TODO LIST: At the end of record have profiler add up the draw stats

void Renderer::Init(
	const Window& window,
	JobSystem& jobSystem)
{
	SetDrawExtent(window.GetExtent());

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
		MAX_INSTANCE_SIZE_GPU_BYTES +
		MAX_INDIRECT_SIZE_GPU_BYTES +
		MAX_TRANSFORMS_SIZE_GPU_BYTES +
		MAX_LIGHTS_SIZE_GPU_BYTES +
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
		LUMINANCE_GROUPS_SIZE_GPU_BYTES,
		m_allocator);

	m_clusterBufferSizes.UpdateClusterBufferSizes(m_drawExtent.Width(), m_drawExtent.Height());
	m_cmaa2BufferSizes.UpdateCmaa2BufferSizes(m_drawExtent.Width(), m_drawExtent.Height());

	// ===================
	// === Image setup ===

	m_bindlessImageTable.Init(
		{ m_drawExtent.Width(), m_drawExtent.Height(), 1u },
		Environment::_HDRPathCount,
		m_device->GetContext().device,
		m_allocator);

	m_bindlessImageTable.PreallocateEquirects(Environment::_HDRPaths, m_allocator);

	// ===============================
	// === Global Data processing ====

	const size_t globalStagingSize = m_allocator.CalcGlobalStagingSize(m_bindlessImageTable);
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
			LUMINANCE_GROUPS_SIZE_GPU_BYTES,
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
		m_bindlessImageTable.BuildCombinedSamplerArray();
	});

	jobSystem.SubmitJob([&](ThreadContext& threadCtx) {
		m_bindlessImageTable.BuildSamplerCubeArray();
	});

	jobSystem.Wait();

	CheckGlobalDescriptorSetSync();

	World::Init(m_bindlessImageTable, false);

	m_bindlessImageTable.FreeEquirects(m_allocator);

	CreateRenderGraph();

	m_profiler.SetVRAMUsage(m_allocator.GetTotalVRAMUsage());
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
			static_cast<size_t>(sizeof(RD::RenderToggles)));
		last = cur;

		updateSet = true;
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

void Renderer::UpdateRendererContext(GLFWwindow* window)
{
	auto& frameCtx = GetCurrentFrame();

	auto& debug = m_profiler.debugToggles;

	World::UpdateWorldState(frameCtx, m_allocator, m_profiler, window);
	//World::UpdateDrawData(frameCtx, m_registeredMeshes.GetMeshes(), m_registeredMeshes.GetLods(), m_profiler);

	const auto& scene = World::GetScene();

	auto& forwardPush = m_profiler.forwardPush;
	auto& lumaPush = m_profiler.lumaExposureSettings;
	auto& smaaPush = m_profiler.smaaTexturesIds;

	uint32_t tilesX = m_drawExtent.Width() / 16u;
	uint32_t tilesY = m_drawExtent.Height() / 16u;
	lumaPush.totalLumaTiles = tilesX * tilesY;
	lumaPush.cameraExposure = m_profiler.toneMappingSettings.cameraExposure;

	if (smaaPush.id0 == UINT32_MAX && smaaPush.id1 == UINT32_MAX)
	{
		smaaPush.id0 = m_bindlessImageTable.GetStaticTexture(RD::Renderer_Texture::SMAASearch).m_bindlessID;
		smaaPush.id1 = m_bindlessImageTable.GetStaticTexture(RD::Renderer_Texture::SMAAArea).m_bindlessID;
	}

	if (m_activeEnvSet != debug.activeEnvMap)
	{
		m_activeEnvSet = debug.activeEnvMap;

		const auto& envSet = m_bindlessImageTable.GetEnvironmentSet(m_activeEnvSet);
		forwardPush.diffuseID = envSet.irradiance.m_bindlessID;
		forwardPush.specularID = envSet.specular.m_bindlessID;

		forwardPush.brdfID = m_bindlessImageTable.GetStaticTexture(RD::Renderer_Texture::Brdf).m_bindlessID;
	}

	if (forwardPush.flashlightCookieTexID == UINT32_MAX && forwardPush.flashlightShadowMapID == UINT32_MAX)
	{
		forwardPush.flashlightCookieTexID = LightingSystem::_mainFlashLight.m_cookieGoboID;
		forwardPush.flashlightShadowMapID = LightingSystem::_mainFlashLight.m_shadowMapID;
	}

	forwardPush.flashlightVP = LightingSystem::_mainFlashLight.ViewProj;
	forwardPush.activeLightCount = LightingSystem::GetActiveLightCount();

	bool postAACopyNeeded =
		(debug.aaMode != static_cast<uint32_t>(RD::AntiAliasingMethod::AA_OFF) &&
		debug.aaMode != static_cast<uint32_t>(RD::AntiAliasingMethod::AA_TAA));

	frameCtx.SetTemporalResult(scene.GetTemporalResult() && !IsFirstFrame());

	m_renderGraphState.frameNumber = m_frameNumber;
	m_renderGraphState.bIsOpaqueVisible = frameCtx.IsOpaqueVisible();
	m_renderGraphState.bIsTransparentVisible = frameCtx.IsTransparentVisible();
	m_renderGraphState.bTemporalValid = frameCtx.IsTemporalValid();
	m_renderGraphState.bCopyPostAAImage = postAACopyNeeded;
	m_renderGraphState.bShowImgui = ShouldRenderImgui();

	// These two are implied together
	m_renderGraphState.activeLightCount = forwardPush.activeLightCount;
	m_renderGraphState.bFlashlightOn = LightingSystem::_mainFlashLight.IsFlashLightOn();

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

		for (uint32_t passIndex = 1; passIndex < TIMESTAMP_PASS_COUNT; ++passIndex)
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

	// Handles initialization and any updates during runtime
	if (frameCtx.m_gpuAddressTable.IsTableDirty())
	{
		frameCtx.m_gpuAddressTable.UpdateCpuVersion();
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
		.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT
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
			.stageMask =
				VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT |
				VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT |
				VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT // earliest consumers of uploaded data
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

		// Resize swapchain, update window, recreate render targets with new extent
		return m_bHasDrawExtentResized;
	}
	else
	{
		INVARIANT(swapResult == VK_SUCCESS);
	}

	m_frameNumber++;
	return m_bHasDrawExtentResized; // Should be false
}

void Renderer::TickVramUsage()
{
	if (m_profiler.getStats().vramQueryTimerSeconds >= 10.0f)
	{
		m_profiler.getStats().vramQueryTimerSeconds -= 10.0f;
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
			BufferBarriers::TransferWriteToGraphicsRead(
				frameCtx.m_commandBuffer,
				addrTable.GetGPUBuffer(RD::Renderer_Buffer::PrevTransforms),
				m_device->GetContext());
		}

		BufferBarriers::TransferWriteToGraphicsRead(
			frameCtx.m_commandBuffer,
			addrTable.GetGPUBuffer(RD::Renderer_Buffer::Transforms),
			m_device->GetContext());

		frameCtx.m_bTransformsBufferUploadNeeded = false;
	}

	if (frameCtx.m_bLightsBufferUploadNeeded)
	{
		// Compute always reads this first
		BufferBarriers::ComputeWriteToRead(
			frameCtx.m_commandBuffer,
			addrTable.GetGPUBuffer(RD::Renderer_Buffer::Lights));


		frameCtx.m_bLightsBufferUploadNeeded = false;
	}

	if (frameCtx.IsOpaqueVisible())
	{
		BufferBarriers::TransferWriteToGraphicsRead(
			frameCtx.m_commandBuffer,
			addrTable.GetGPUBuffer(RD::Renderer_Buffer::VisibleInstances),
			m_device->GetContext());
		BufferBarriers::TransferWriteToIndirectRead(
			frameCtx.m_commandBuffer,
			addrTable.GetGPUBuffer(RD::Renderer_Buffer::IndirectDraws),
			m_device->GetContext());
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

	m_renderGraph.ExecutePasses(m_renderPassExecutionContext);

	TimestampPoolEnd(frameCtx);

	VK_CHECK(vkEndCommandBuffer(frameCtx.m_commandBuffer));
}

void Renderer::StallDevice()
{
	m_device->IdleDevice();
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
