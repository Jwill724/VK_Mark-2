#include "pch.h"

// some circular shit happening with these two
// but it works
#include "EngineState.h"
#include "Engine.h"

#include "utils/VulkanUtils.h"
#include "utils/BufferUtils.h"
#include "core/Environment.h"
#include "JobSystem.h"
#include "renderer/Renderer.h"
#include "renderer/scene/RenderScene.h"
#include "profiler/EditorImgui.h"
#include "core/loader/MeshLoader.h"

std::vector<ThreadContext>& allThreadContexts = GetAllThreadContexts();

void EngineState::Init() {
	const auto device = Backend::GetDevice();

	JobSystem::InitScheduler();

	uint32_t graphicsIndex = Backend::GetGraphicsQueue().familyIndex;
	uint32_t transferIndex = Backend::GetTransferQueue().familyIndex;

	JobSystem::GetThreadPoolManager().Init(device, static_cast<uint32_t>(allThreadContexts.size()), graphicsIndex, transferIndex);

	_resources.Init(device);
	auto& dQueue = _resources.GetMainDQueue();
	auto mainAllocator = _resources.GetAllocator();

	if (!Backend::IsComputeAvailable()) {
		Engine::GetProfiler().disableGPUAccelUsage();
	}

	EditorImgui::InitImgui(dQueue);

	DescriptorSetOverwatch::InitDescriptors(device, dQueue);

	const auto& winExtent = Engine::GetWindowExtent();
	Renderer::SetDrawExtent({ winExtent.width, winExtent.height, 1u });

	auto& tempQueue = _resources.GetTempDQueue();

	ResourceManager::InitUniformRenderTargets(
		device,
		_resources.GetRenderTargetDQueue(),
		mainAllocator,
		Renderer::GetDrawExtent());
	ResourceManager::InitRenderSamplers(device, dQueue);
	ResourceManager::InitShadowMapImages(device, dQueue, mainAllocator);
	ResourceManager::InitTextures(device, _resources.GetGraphicsPool(), dQueue, tempQueue, mainAllocator);
	ResourceManager::InitStaticEnvironmentImages(device, dQueue, mainAllocator);

	// Pipelines init
	PipelineManager::DefinePipelineLayout();
	PipelineManager::InitPipelines(dQueue);

	Environment::dispatchEnvironmentMaps(
		device,
		_resources);

	VK_CHECK(vkResetCommandPool(device, _resources.GetGraphicsPool(), 0));

	// Setup descriptor resources

	auto& globalImgManager = ResourceManager::_globalImageManager;
	auto& engineProfiler = Engine::GetProfiler();

	const auto linearClampSampler = ResourceManager::GetLinearClamp_Sampler();
	const auto linearLODClampSampler = ResourceManager::GetLinearLODClamp_Sampler();
	const auto shadowSampler = ResourceManager::GetShadowMap_Sampler();
	const auto noiseSampler = ResourceManager::GetNoise_Sampler();

	// CSM image
	auto& shadowImg = ResourceManager::GetDirectionalCSMAtlas_Target();
	shadowImg.lutEntry.combinedImageIndex = globalImgManager.AddCombinedImage(shadowImg.imageView, shadowSampler);
	_resources.AddImageLUTEntry(ImageLUTEntry::CombinedOnly(shadowImg.lutEntry.combinedImageIndex));

	// flashlight shadow image
	auto& flashlightShadowImg = ResourceManager::GetFlashlightShadowMap_Target();
	flashlightShadowImg.lutEntry.combinedImageIndex = globalImgManager.AddCombinedImage(flashlightShadowImg.imageView, shadowSampler);
	_resources.AddImageLUTEntry(ImageLUTEntry::CombinedOnly(flashlightShadowImg.lutEntry.combinedImageIndex));

	// cookie gobo image
	auto& cookieGoboImg = ResourceManager::GetCookieGobo_Texture();
	cookieGoboImg.lutEntry.combinedImageIndex = globalImgManager.AddCombinedImage(cookieGoboImg.imageView, linearClampSampler);
	_resources.AddImageLUTEntry(ImageLUTEntry::CombinedOnly(cookieGoboImg.lutEntry.combinedImageIndex));

	// Rainbow LUT
	auto& rainbowLut = ResourceManager::GetRainbowLUT_Texture();
	rainbowLut.lutEntry.combinedImageIndex = globalImgManager.AddCombinedImage(rainbowLut.imageView, linearClampSampler);
	_resources.AddImageLUTEntry(ImageLUTEntry::CombinedOnly(rainbowLut.lutEntry.combinedImageIndex));
	engineProfiler.lensFlareSettings.rainbowLUTIndex = rainbowLut.lutEntry.combinedImageIndex;

	// search and area lut textures
	auto& searchTex = ResourceManager::GetSearchSMAA_Texture();
	searchTex.lutEntry.combinedImageIndex = globalImgManager.AddCombinedImage(searchTex.imageView, linearLODClampSampler);
	_resources.AddImageLUTEntry(ImageLUTEntry::CombinedOnly(searchTex.lutEntry.combinedImageIndex));
	_resources.smaaTextures.id0 = searchTex.lutEntry.combinedImageIndex;

	auto& areaTex = ResourceManager::GetAreaSMAA_Texture();
	areaTex.lutEntry.combinedImageIndex = globalImgManager.AddCombinedImage(areaTex.imageView, linearLODClampSampler);
	_resources.AddImageLUTEntry(ImageLUTEntry::CombinedOnly(areaTex.lutEntry.combinedImageIndex));
	_resources.smaaTextures.id1 = areaTex.lutEntry.combinedImageIndex;

	// hilbert curve lut
	auto& hilbertLut = ResourceManager::GetHilbertCurveLUT_Texture();
	hilbertLut.lutEntry.combinedImageIndex = globalImgManager.AddCombinedImage(hilbertLut.imageView, noiseSampler);
	_resources.AddImageLUTEntry(ImageLUTEntry::CombinedOnly(hilbertLut.lutEntry.combinedImageIndex));
	engineProfiler.ssaoSettings.hilbertLutID = hilbertLut.lutEntry.combinedImageIndex;

	// === ENVIRONMENT IMAGE SETUP ===
	auto skyboxSmpl = ResourceManager::GetSkyBox_Sampler();
	auto irradianceSmpl = ResourceManager::GetIrradiance_Sampler();
	auto specSmpl = ResourceManager::GetSpecularPrefilter_Sampler();
	auto& brdfImg = ResourceManager::GetBRDF_Texture();
	auto brdfSmpl = ResourceManager::GetBRDF_Sampler();

	brdfImg.lutEntry.combinedImageIndex = globalImgManager.AddCombinedImage(brdfImg.imageView, brdfSmpl);
	_resources.AddImageLUTEntry(ImageLUTEntry::CombinedOnly(brdfImg.lutEntry.combinedImageIndex));

	for (auto& env : ResourceManager::_environmentSets) {
		if (env.setIndex == UINT32_MAX) break;

		auto& irradianceImg = env.irradiance;
		auto& specularImg = env.specular;
		auto& skyboxImg = env.skybox;

		irradianceImg.lutEntry.samplerCubeIndex = globalImgManager.AddCubeImage(irradianceImg.imageView, irradianceSmpl);
		_resources.AddImageLUTEntry(ImageLUTEntry::SamplerCubeOnly(irradianceImg.lutEntry.samplerCubeIndex));

		specularImg.lutEntry.samplerCubeIndex = globalImgManager.AddCubeImage(specularImg.imageView, specSmpl);
		_resources.AddImageLUTEntry(ImageLUTEntry::SamplerCubeOnly(specularImg.lutEntry.samplerCubeIndex));

		skyboxImg.lutEntry.samplerCubeIndex = globalImgManager.AddCubeImage(skyboxImg.imageView, skyboxSmpl);
		_resources.AddImageLUTEntry(ImageLUTEntry::SamplerCubeOnly(skyboxImg.lutEntry.samplerCubeIndex));

		glm::uvec4 envEntry{};
		envEntry.x = irradianceImg.lutEntry.samplerCubeIndex;
		envEntry.y = specularImg.lutEntry.samplerCubeIndex;
		envEntry.z = brdfImg.lutEntry.combinedImageIndex;
		envEntry.w = skyboxImg.lutEntry.samplerCubeIndex;

		ASSERT(env.setIndex < MAX_ENV_SETS && "Too many environment sets for fixed UBO buffer!");
		ResourceManager::_envMapIdxArray.indices[env.setIndex] = envEntry;
	}

	_resources.envMapIndexBuffer = BufferUtils::CreateUniformBuffer(ResourceManager::_envMapIdxArray, mainAllocator);

	// main address table buffer
	_resources.GetAddressTableBuffer() = BufferUtils::CreateBuffer(
		sizeof(_resources.GetAddressTable().GetTable()),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
		VK_BUFFER_USAGE_TRANSFER_DST_BIT |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY,
		mainAllocator
	);

	// LUMINANCE SSBO SETUP

	AllocatedBuffer luminanceStaging = BufferUtils::CreateBuffer(
		luminanceSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
		mainAllocator
	);

	memcpy(luminanceStaging.m_allocInfo.pMappedData, &ResourceManager::_luminanceSums, luminanceSize);

	AllocatedBuffer luminanceBuffer = BufferUtils::CreateGPUAddressBuffer(
		BufferSlot::Luminance,
		_resources.GetAddressTable(),
		luminanceSize,
		mainAllocator
	);
	_resources.AddGPUBufferToGlobalAddress(BufferSlot::Luminance, luminanceBuffer);

	auto lBuf = luminanceStaging.m_buffer;
	auto lAlloc = luminanceStaging.m_allocation;
	tempQueue.PushFunction([lBuf, lAlloc, mainAllocator]() mutable {
		BufferUtils::DestroyBuffer(lBuf, lAlloc, mainAllocator);
	});

	JobSystem::SubmitJob([&luminanceBuffer, &luminanceStaging, device](ThreadContext& threadCtx) {
		threadCtx.cmdPool = JobSystem::GetThreadPoolManager().GetPool(threadCtx.threadID, QueueType::Transfer);

		CommandBuffer::RecordDeferredCmd([&](VkCommandBuffer cmd) {
			VkBufferCopy copyRegion{};
			copyRegion.size = luminanceSize;
			vkCmdCopyBuffer(cmd, luminanceStaging.m_buffer, luminanceBuffer.m_buffer, 1, &copyRegion);

		}, threadCtx.cmdPool, QueueType::Transfer, device);

		auto& tQueue = Backend::GetTransferQueue();
		threadCtx.lastSubmittedFence = Engine::GetState().submitCommandBuffers(tQueue);
		waitAndRecycleLastFence(threadCtx.lastSubmittedFence, tQueue, device);
		vkResetCommandPool(device, threadCtx.cmdPool, 0)
		threadCtx.cmdPool = VK_NULL_HANDLE;
	});

	JobSystem::Wait();

	tempQueue.flush();

#ifdef TRACY_ENABLE
	auto& profiler = Engine::GetProfiler();
	profiler.getTracyGraphicsCmd() = CommandBuffer::CreateCommandBuffer(device, _resources.GetGraphicsPool());
	profiler.initTracyGPU(
		Backend::GetPhysicalDevice(),
		device,
		Backend::GetGraphicsQueue().queue,
		profiler.getTracyGraphicsCmd());
#endif

	// === GLOBAL DESCRIPTOR SETUP ===
	// Global descriptor writing and update
	auto unifiedSet = DescriptorSetOverwatch::GetUnifiedDescriptor().descriptorSet;
	DescriptorWriter mainWriter;
	mainWriter.WriteBuffer(
		ADDRESS_TABLE_BINDING,
		_resources.GetAddressTablewBuffer().m_buffer,
		sizeof(BindlessBDATable),
		0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		unifiedSet);

	// env map index
	mainWriter.WriteBuffer(
		GLOBAL_BINDING_ENV_INDEX,
		_resources.envMapIndexBuffer.m_buffer,
		sizeof(EnvironmentIndexArray),
		0,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		unifiedSet);

	mainWriter.WriteFromImageLUT(_resources.GetLUTManager().GetEntries(), globalImgManager.table);
	mainWriter.WriteBindlessImages(GLOBAL_BINDING_SAMPLER_CUBE, DescriptorImageType::SamplerCube, unifiedSet);
	mainWriter.WriteBindlessImages(GLOBAL_BINDING_COMBINED_SAMPLER, DescriptorImageType::CombinedSampler, unifiedSet);
	mainWriter.UpdateSet(device, unifiedSet);

	LightingSystem::init(_resources);
}

void EngineState::loadAssets(Profiler& engineProfiler) {
	auto assetQueue = std::make_shared<GLTFAssetQueue>();

	bool availableAssets = false;
	// Load files for assets and upload any default global buffers
	JobSystem::SubmitJob([assetQueue, &availableAssets](ThreadContext& threadCtx) {
		ScopedWorkQueue scoped(threadCtx, assetQueue.get());
		availableAssets = AssetManager::loadGltf(threadCtx);
	});

	JobSystem::Wait();

	if (availableAssets) {
		const auto mainAllocator = _resources.GetAllocator();
		const auto device = Backend::GetDevice();
		auto& tempQueue = _resources.GetTempDQueue();

		fmt::println("\nAssets available for loading!");
		engineProfiler.startTimer();

		// === TEXTURE LOADING ===
		JobSystem::SubmitJob([assetQueue, mainAllocator, device, &tempQueue](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			threadCtx.cmdPool = JobSystem::GetThreadPoolManager().GetPool(threadCtx.threadID, QueueType::Graphics);
			AssetManager::decodeImages(threadCtx, mainAllocator, tempQueue, device);
			auto& gQueue = Backend::GetGraphicsQueue();

			threadCtx.lastSubmittedFence = Engine::GetState().submitCommandBuffers(gQueue);
			waitAndRecycleLastFence(threadCtx.lastSubmittedFence, gQueue, device);
			vkResetCommandPool(device, threadCtx.cmdPool, 0);
			threadCtx.cmdPool = VK_NULL_HANDLE;
		});

		JobSystem::Wait();
		JobSystem::FlushLogs();

		// === SAMPLER CREATION ===
		JobSystem::SubmitJob([assetQueue](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			AssetManager::buildSamplers(threadCtx);
		});

		JobSystem::Wait();
		JobSystem::FlushLogs();

		// === MATERIAL PROCESSING ===
		JobSystem::SubmitJob([assetQueue, mainAllocator, device](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			threadCtx.cmdPool = JobSystem::GetThreadPoolManager().GetPool(threadCtx.threadID, QueueType::Transfer);
			AssetManager::processMaterials(threadCtx, mainAllocator, device);

			auto& tQueue = Backend::GetTransferQueue();

			threadCtx.lastSubmittedFence = Engine::GetState().submitCommandBuffers(tQueue);
			waitAndRecycleLastFence(threadCtx.lastSubmittedFence, tQueue, device);
			vkResetCommandPool(device, threadCtx.cmdPool, 0);
			threadCtx.cmdPool = VK_NULL_HANDLE;
		});

		JobSystem::Wait();
		JobSystem::FlushLogs();

		// === MESH PROCESS ===
		auto& meshes = _resources.GetResgisteredMeshes();
		std::vector<Vertex> totalVertices;
		std::vector<uint32_t> totalIndices;

		auto& modelDataCounts = _resources.modelDataCounts;
		JobSystem::SubmitJob([assetQueue, &meshes, &totalVertices, &totalIndices, &modelDataCounts](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			AssetManager::processMeshes(
				threadCtx,
				meshes,
				totalVertices,
				totalIndices,
				modelDataCounts);
		});

		JobSystem::Wait();
		JobSystem::FlushLogs();

		// === MESH UPLOAD ===
		JobSystem::SubmitJob([mainAllocator, device, &meshes, &totalVertices, &totalIndices](ThreadContext& threadCtx) {
			threadCtx.cmdPool = JobSystem::GetThreadPoolManager().GetPool(threadCtx.threadID, QueueType::Transfer);

			MeshLoader::uploadMeshes(
				threadCtx,
				totalVertices,
				totalIndices,
				meshes,
				mainAllocator,
				device);
		});

		// === SCENE GRAPH BUILD ===
		JobSystem::SubmitJob([assetQueue, &modelDataCounts](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			AssetManager::buildSceneGraph(
				threadCtx,
				RenderScene::_globalInstances,
				RenderScene::_globalTransforms,
				modelDataCounts);

			auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
			ASSERT(queue && "queue broke.");

			auto gltfJobs = queue->Collect();

			for (auto& context : gltfJobs) {
				if (!context->isComplete()) continue;
				auto& scene = *context->scene;

				if (!context->hasRegisteredScene) {
					RenderScene::_loadedScenes[static_cast<SceneID>(scene.sceneID)] = context->scene;
					JobSystem::Log(threadCtx.threadID, fmt::format("Registered scene '{}'\n", scene.sceneName));
					context->hasRegisteredScene = true;
				}
			}
		});

		JobSystem::Wait();
		JobSystem::FlushLogs();

		// Asset loading done
		auto elapsed = engineProfiler.endTimerSec();
		fmt::print("Asset loading completed in {:.3f} seconds.\n\n", elapsed);

		// flush any setup temp data like staging buffers
		tempQueue.flush();

		// Update global descriptors
		auto& globalImgManager = ResourceManager::_globalImageManager;
		auto unifiedSet = DescriptorSetOverwatch::GetUnifiedDescriptor().descriptorSet;
		DescriptorWriter mainWriter;
		mainWriter.WriteFromImageLUT(_resources.GetLUTManager().GetEntries(), globalImgManager.table);
		mainWriter.WriteBindlessImages(GLOBAL_BINDING_COMBINED_SAMPLER, DescriptorImageType::CombinedSampler, unifiedSet);
		mainWriter.UpdateSet(device, unifiedSet);
	}
	else {
		fmt::print("No assets for loading... skipping\n\n");
	}

	_resources.assetsLoaded = availableAssets;
}

void EngineState::initRenderer(Profiler& engineProfiler) {
	const auto device = Backend::GetDevice();

	_resources.UpdateAddressTableMapped();

	Renderer::initRenderer(
		device,
		DescriptorSetOverwatch::GetFrameDescriptor().descriptorLayout,
		_resources,
		engineProfiler
	);

	RenderScene::setScene(_resources.assetsLoaded);

	// GPU name
	// This can be defined whenever before render
	engineProfiler.getStats().gpuName = Backend::GetDeviceName();

	// VRAM Usage calculator
	engineProfiler.getStats().vramStats = engineProfiler.getTotalVRAMUsage(Backend::GetPhysicalDevice(), _resources.GetAllocator());
}


void EngineState::renderFrame(Profiler& engineProfiler) {
	auto& frame = Renderer::GetCurrentFrame();
	auto& stats = engineProfiler.getStats();
	// Every 5 seconds
	if (stats.vramQueryTimerSeconds >= 5.0f) {
		stats.vramQueryTimerSeconds -= 5.0f;

		stats.vramStats = engineProfiler.getTotalVRAMUsage(
			Backend::GetPhysicalDevice(),
			_resources.GetAllocator());
	}

	const auto& debug = engineProfiler.debugToggles;
	EditorImgui::renderImgui(engineProfiler);

	Renderer::prepareFrameContext(frame, _resources.GetAllocator());
	if (frame.m_swapchainResult != VK_SUCCESS) return;

	engineProfiler.resetDrawCalls();
	// === Scene update ===
	engineProfiler.startTimer();
	RenderScene::updateScene(frame, _resources, debug);
	engineProfiler.getStats().sceneUpdateTime.add(engineProfiler.endTimerMS());

	// === Draw commands ===
	engineProfiler.startTimer();
	Renderer::recordRenderCommand(frame, engineProfiler);
	engineProfiler.getStats().drawTime.add(engineProfiler.endTimerMS());

	Renderer::submitFrame(frame, _resources);
}

void EngineState::shutdown() {
	const auto device = Backend::GetDevice();
	const auto alloc = _resources.GetAllocator();

	JobSystem::ShutdownScheduler();

#ifdef TRACY_ENABLE
	Engine::GetProfiler().shutdownTracyGPU();
#endif

	LightingSystem::Cleanup();
	RenderScene::cleanScene();

	JobSystem::GetThreadPoolManager().Cleanup(device);

	for (uint32_t i = 0; i < static_cast<int>(allThreadContexts.size()); ++i) {
		ThreadContext& threadCtx = allThreadContexts[i];
		threadCtx.deletionQueue.Flush();
		ASSERT(threadCtx.cmdPool == VK_NULL_HANDLE);
		ASSERT(threadCtx.lastSubmittedFence == VK_NULL_HANDLE);
		ASSERT(threadCtx.stagingMapped == nullptr);
	}

	_resources.GetTempDQueue().flush();
	_resources.GetMainDQueue().flush();
	_resources.GetRenderTargetDQueue().flush();
	_resources.GetDynamicPipelineQueue().flush();
	_resources.GetDynamicPipelineShaderStagesQueue().flush();

	Renderer::cleanupRenderer(device, alloc);

	_resources.Cleanup(device);
}


//VkFence EngineState::submitCommandBuffers(GPUQueue& queue) {
//	std::vector<VkCommandBuffer> cmds{};
//
//	switch (queue.qType) {
//	case QueueType::Graphics:
//		cmds = DeferredCmdSubmitQueue::collectGraphics();
//		if (cmds.empty()) {
//			fmt::print("No graphics commands.\n");
//			return VK_NULL_HANDLE;
//		}
//		break;
//
//	case QueueType::Transfer:
//		cmds = DeferredCmdSubmitQueue::collectTransfer();
//		if (cmds.empty()) {
//			fmt::print("No transfer commands.\n");
//			return VK_NULL_HANDLE;
//		}
//		break;
//
//	case QueueType::Compute:
//		cmds = DeferredCmdSubmitQueue::collectCompute();
//		if (cmds.empty()) {
//			fmt::print("No compute commands.\n");
//			return VK_NULL_HANDLE;
//		}
//		break;
//
//	default:
//		ASSERT(false && "Invalid queue type!");
//		return VK_NULL_HANDLE;
//	}
//
//	if (cmds.empty()) {
//		fmt::print("Command queue is empty for type {}.\n", static_cast<uint8_t>(queue.qType));
//		return VK_NULL_HANDLE;
//	}
//
//	VkSubmitInfo submitInfo{};
//	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
//	submitInfo.commandBufferCount = static_cast<uint32_t>(cmds.size());
//	submitInfo.pCommandBuffers = cmds.data();
//
//	VkFence lastSubmittedFence = queue.submit(submitInfo);
//	return lastSubmittedFence;
//}
