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
#include "platform/profiler/EditorImgui.h"
#include "core/loader/MeshLoader.h"

std::vector<ThreadContext>& allThreadContexts = getAllThreadContexts();

void EngineState::init() {
	const auto device = Backend::getDevice();

	JobSystem::initScheduler();

	uint32_t graphicsIndex = Backend::getGraphicsQueue().familyIndex;
	uint32_t transferIndex = Backend::getTransferQueue().familyIndex;

	JobSystem::getThreadPoolManager().init(device, static_cast<uint32_t>(allThreadContexts.size()), graphicsIndex, transferIndex);

	_resources.init(device);
	auto& dQueue = _resources.getMainDQueue();
	auto mainAllocator = _resources.getAllocator();

	if (!Backend::isComputeAvailable()) {
		Engine::getProfiler().disableGPUAccelUsage();
	}

	EditorImgui::initImgui(dQueue);

	DescriptorSetOverwatch::initDescriptors(device, dQueue);

	const auto& winExtent = Engine::getWindowExtent();
	Renderer::setDrawExtent({ winExtent.width, winExtent.height, 1u });

	auto& tempQueue = _resources.getTempDQueue();

	ResourceManager::initUniformRenderTargets(
		device,
		_resources.getRenderTargetDQueue(),
		mainAllocator,
		Renderer::getDrawExtent());
	ResourceManager::initRenderSamplers(device, dQueue);
	ResourceManager::initShadowMapImages(device, dQueue, mainAllocator);
	ResourceManager::initTextures(device, _resources.getGraphicsPool(), dQueue, tempQueue, mainAllocator);
	ResourceManager::initStaticEnvironmentImages(device, dQueue, mainAllocator);

	// Pipelines init
	PipelineManager::definePipelineData();
	PipelineManager::initPipelines(dQueue);

	Environment::dispatchEnvironmentMaps(
		device,
		_resources);

	VK_CHECK(vkResetCommandPool(device, _resources.getGraphicsPool(), 0));

	// Setup descriptor resources

	auto& globalImgManager = ResourceManager::_globalImageManager;
	auto& engineProfiler = Engine::getProfiler();

	const auto linearClampSampler = ResourceManager::getLinearClamp_Sampler();
	const auto linearLODClampSampler = ResourceManager::getLinearLODClamp_Sampler();
	const auto shadowSampler = ResourceManager::getShadowMap_Sampler();
	const auto noiseSampler = ResourceManager::getNoise_Sampler();

	// CSM image
	auto& shadowImg = ResourceManager::getDirectionalCSMAtlas_Target();
	shadowImg.lutEntry.combinedImageIndex = globalImgManager.addCombinedImage(shadowImg.imageView, shadowSampler);
	_resources.addImageLUTEntry(ImageLUTEntry::CombinedOnly(shadowImg.lutEntry.combinedImageIndex));

	// flashlight shadow image
	auto& flashlightShadowImg = ResourceManager::getFlashLightShadowMap_Target();
	flashlightShadowImg.lutEntry.combinedImageIndex = globalImgManager.addCombinedImage(flashlightShadowImg.imageView, shadowSampler);
	_resources.addImageLUTEntry(ImageLUTEntry::CombinedOnly(flashlightShadowImg.lutEntry.combinedImageIndex));

	// cookie gobo image
	auto& cookieGoboImg = ResourceManager::getCookieGobo_Texture();
	cookieGoboImg.lutEntry.combinedImageIndex = globalImgManager.addCombinedImage(cookieGoboImg.imageView, linearClampSampler);
	_resources.addImageLUTEntry(ImageLUTEntry::CombinedOnly(cookieGoboImg.lutEntry.combinedImageIndex));

	// Rainbow LUT
	auto& rainbowLut = ResourceManager::getRainbowLUT_Texture();
	rainbowLut.lutEntry.combinedImageIndex = globalImgManager.addCombinedImage(rainbowLut.imageView, linearClampSampler);
	_resources.addImageLUTEntry(ImageLUTEntry::CombinedOnly(rainbowLut.lutEntry.combinedImageIndex));
	engineProfiler.lensFlareSettings.rainbowLUTIndex = rainbowLut.lutEntry.combinedImageIndex;

	// search and area lut textures
	auto& searchTex = ResourceManager::getSearchSMAA_Texture();
	searchTex.lutEntry.combinedImageIndex = globalImgManager.addCombinedImage(searchTex.imageView, linearLODClampSampler);
	_resources.addImageLUTEntry(ImageLUTEntry::CombinedOnly(searchTex.lutEntry.combinedImageIndex));
	_resources.smaaTextures.id0 = searchTex.lutEntry.combinedImageIndex;

	auto& areaTex = ResourceManager::getAreaSMAA_Texture();
	areaTex.lutEntry.combinedImageIndex = globalImgManager.addCombinedImage(areaTex.imageView, linearLODClampSampler);
	_resources.addImageLUTEntry(ImageLUTEntry::CombinedOnly(areaTex.lutEntry.combinedImageIndex));
	_resources.smaaTextures.id1 = areaTex.lutEntry.combinedImageIndex;

	// === ENVIRONMENT IMAGE SETUP ===
	auto skyboxSmpl = ResourceManager::getSkyBox_Sampler();
	auto irradianceSmpl = ResourceManager::getIrradiance_Sampler();
	auto specSmpl = ResourceManager::getSpecularPrefilter_Sampler();
	auto& brdfImg = ResourceManager::getBRDF_Texture();
	auto brdfSmpl = ResourceManager::getBRDF_Sampler();

	brdfImg.lutEntry.combinedImageIndex = globalImgManager.addCombinedImage(brdfImg.imageView, brdfSmpl);
	_resources.addImageLUTEntry(ImageLUTEntry::CombinedOnly(brdfImg.lutEntry.combinedImageIndex));

	for (auto& env : ResourceManager::_environmentSets) {
		if (env.setIndex == UINT32_MAX) break;

		auto& irradianceImg = env.irradiance;
		auto& specularImg = env.specular;
		auto& skyboxImg = env.skybox;

		irradianceImg.lutEntry.samplerCubeIndex = globalImgManager.addCubeImage(irradianceImg.imageView, irradianceSmpl);
		_resources.addImageLUTEntry(ImageLUTEntry::SamplerCubeOnly(irradianceImg.lutEntry.samplerCubeIndex));

		specularImg.lutEntry.samplerCubeIndex = globalImgManager.addCubeImage(specularImg.imageView, specSmpl);
		_resources.addImageLUTEntry(ImageLUTEntry::SamplerCubeOnly(specularImg.lutEntry.samplerCubeIndex));

		skyboxImg.lutEntry.samplerCubeIndex = globalImgManager.addCubeImage(skyboxImg.imageView, skyboxSmpl);
		_resources.addImageLUTEntry(ImageLUTEntry::SamplerCubeOnly(skyboxImg.lutEntry.samplerCubeIndex));

		glm::uvec4 envEntry{};
		envEntry.x = irradianceImg.lutEntry.samplerCubeIndex;
		envEntry.y = specularImg.lutEntry.samplerCubeIndex;
		envEntry.z = brdfImg.lutEntry.combinedImageIndex;
		envEntry.w = skyboxImg.lutEntry.samplerCubeIndex;

		ASSERT(env.setIndex < MAX_ENV_SETS && "Too many environment sets for fixed UBO buffer!");
		ResourceManager::_envMapIdxArray.indices[env.setIndex] = envEntry;
	}

	_resources.envMapIndexBuffer = BufferUtils::createUniformBuffer(ResourceManager::_envMapIdxArray, mainAllocator);

	// main address table buffer
	_resources.getAddressTableBuffer() = BufferUtils::createBuffer(
		sizeof(GPUAddressTable),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
		VK_BUFFER_USAGE_TRANSFER_DST_BIT |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY,
		mainAllocator
	);

	// LUMINANCE SSBO SETUP
	const size_t luminanceSize = sizeof(glm::vec4[MAX_LUMINANCE_GROUPS]);

	AllocatedBuffer luminanceStaging = BufferUtils::createBuffer(
		luminanceSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
		mainAllocator
	);

	memcpy(luminanceStaging.info.pMappedData, &ResourceManager::_luminanceSums, luminanceSize);

	AllocatedBuffer luminanceBuffer = BufferUtils::createGPUAddressBuffer(
		AddressBufferType::Luminance,
		_resources.getAddressTable(),
		luminanceSize,
		mainAllocator
	);
	_resources.addGPUBufferToGlobalAddress(AddressBufferType::Luminance, luminanceBuffer);

	auto lBuf = luminanceStaging.buffer;
	auto lAlloc = luminanceStaging.allocation;
	tempQueue.push_function([lBuf, lAlloc, mainAllocator]() mutable {
		BufferUtils::destroyBuffer(lBuf, lAlloc, mainAllocator);
	});

	JobSystem::submitJob([&luminanceBuffer, &luminanceStaging, device](ThreadContext& threadCtx) {
		threadCtx.cmdPool = JobSystem::getThreadPoolManager().getPool(threadCtx.threadID, QueueType::Transfer);

		CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {
			VkBufferCopy copyRegion{};
			copyRegion.size = luminanceSize;
			vkCmdCopyBuffer(cmd, luminanceStaging.buffer, luminanceBuffer.buffer, 1, &copyRegion);

		}, threadCtx.cmdPool, QueueType::Transfer, device);

		auto& tQueue = Backend::getTransferQueue();
		threadCtx.lastSubmittedFence = Engine::getState().submitCommandBuffers(tQueue);
		waitAndRecycleLastFence(threadCtx.lastSubmittedFence, tQueue, device);
		vkResetCommandPool(device, threadCtx.cmdPool, 0);
		threadCtx.cmdPool = VK_NULL_HANDLE;
	});

	JobSystem::wait();

	tempQueue.flush();

#ifdef TRACY_ENABLE
	auto& profiler = Engine::getProfiler();
	profiler.getTracyGraphicsCmd() = CommandBuffer::createCommandBuffer(device, _resources.getGraphicsPool());
	profiler.initTracyGPU(
		Backend::getPhysicalDevice(),
		device,
		Backend::getGraphicsQueue().queue,
		profiler.getTracyGraphicsCmd());
#endif

	// === GLOBAL DESCRIPTOR SETUP ===
	// Global descriptor writing and update
	auto unifiedSet = DescriptorSetOverwatch::getUnifiedDescriptor().descriptorSet;
	DescriptorWriter mainWriter;
	mainWriter.writeBuffer(
		ADDRESS_TABLE_BINDING,
		_resources.getAddressTableBuffer().buffer,
		sizeof(GPUAddressTable),
		0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		unifiedSet);

	// env map index
	mainWriter.writeBuffer(
		GLOBAL_BINDING_ENV_INDEX,
		_resources.envMapIndexBuffer.buffer,
		sizeof(GPUEnvMapIndexArray),
		0,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		unifiedSet);

	mainWriter.writeFromImageLUT(_resources.getLUTManager().getEntries(), globalImgManager.table);
	mainWriter.writeImages(GLOBAL_BINDING_SAMPLER_CUBE, DescriptorImageType::SamplerCube, unifiedSet);
	mainWriter.writeImages(GLOBAL_BINDING_COMBINED_SAMPLER, DescriptorImageType::CombinedSampler, unifiedSet);
	mainWriter.updateSet(device, unifiedSet);

	LightingSystem::init(_resources);
}

void EngineState::loadAssets(Profiler& engineProfiler) {
	auto assetQueue = std::make_shared<GLTFAssetQueue>();

	bool availableAssets = false;
	// Load files for assets and upload any default global buffers
	JobSystem::submitJob([assetQueue, &availableAssets](ThreadContext& threadCtx) {
		ScopedWorkQueue scoped(threadCtx, assetQueue.get());
		availableAssets = AssetManager::loadGltf(threadCtx);
	});

	JobSystem::wait();

	if (availableAssets) {
		const auto mainAllocator = _resources.getAllocator();
		const auto device = Backend::getDevice();
		auto& tempQueue = _resources.getTempDQueue();

		fmt::println("\nAssets available for loading!");
		engineProfiler.startTimer();

		// === TEXTURE LOADING ===
		JobSystem::submitJob([assetQueue, mainAllocator, device, &tempQueue](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			threadCtx.cmdPool = JobSystem::getThreadPoolManager().getPool(threadCtx.threadID, QueueType::Graphics);
			AssetManager::decodeImages(threadCtx, mainAllocator, tempQueue, device);
			auto& gQueue = Backend::getGraphicsQueue();

			threadCtx.lastSubmittedFence = Engine::getState().submitCommandBuffers(gQueue);
			waitAndRecycleLastFence(threadCtx.lastSubmittedFence, gQueue, device);
			vkResetCommandPool(device, threadCtx.cmdPool, 0);
			threadCtx.cmdPool = VK_NULL_HANDLE;
		});

		JobSystem::wait();
		JobSystem::flushLogs();

		// === SAMPLER CREATION ===
		JobSystem::submitJob([assetQueue](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			AssetManager::buildSamplers(threadCtx);
		});

		JobSystem::wait();
		JobSystem::flushLogs();

		// === MATERIAL PROCESSING ===
		JobSystem::submitJob([assetQueue, mainAllocator, device](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			threadCtx.cmdPool = JobSystem::getThreadPoolManager().getPool(threadCtx.threadID, QueueType::Transfer);
			AssetManager::processMaterials(threadCtx, mainAllocator, device);

			auto& tQueue = Backend::getTransferQueue();

			threadCtx.lastSubmittedFence = Engine::getState().submitCommandBuffers(tQueue);
			waitAndRecycleLastFence(threadCtx.lastSubmittedFence, tQueue, device);
			vkResetCommandPool(device, threadCtx.cmdPool, 0);
			threadCtx.cmdPool = VK_NULL_HANDLE;
		});

		JobSystem::wait();
		JobSystem::flushLogs();

		// === MESH PROCESS ===
		auto& meshes = _resources.getResgisteredMeshes();
		std::vector<Vertex> totalVertices;
		std::vector<uint32_t> totalIndices;

		auto& modelDataCounts = _resources.modelDataCounts;
		JobSystem::submitJob([assetQueue, &meshes, &totalVertices, &totalIndices, &modelDataCounts](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			AssetManager::processMeshes(
				threadCtx,
				meshes,
				totalVertices,
				totalIndices,
				modelDataCounts);
		});

		JobSystem::wait();
		JobSystem::flushLogs();

		// === MESH UPLOAD ===
		JobSystem::submitJob([mainAllocator, device, &meshes, &totalVertices, &totalIndices](ThreadContext& threadCtx) {
			threadCtx.cmdPool = JobSystem::getThreadPoolManager().getPool(threadCtx.threadID, QueueType::Transfer);

			MeshLoader::uploadMeshes(
				threadCtx,
				totalVertices,
				totalIndices,
				meshes,
				mainAllocator,
				device);
		});

		// === SCENE GRAPH BUILD ===
		JobSystem::submitJob([assetQueue, &modelDataCounts](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			AssetManager::buildSceneGraph(
				threadCtx,
				RenderScene::_globalInstances,
				RenderScene::_globalTransforms,
				modelDataCounts);

			auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
			ASSERT(queue && "queue broke.");

			auto gltfJobs = queue->collect();

			for (auto& context : gltfJobs) {
				if (!context->isComplete()) continue;
				auto& scene = *context->scene;

				if (!context->hasRegisteredScene) {
					RenderScene::_loadedScenes[static_cast<SceneID>(scene.sceneID)] = context->scene;
					JobSystem::log(threadCtx.threadID, fmt::format("Registered scene '{}'\n", scene.sceneName));
					context->hasRegisteredScene = true;
				}
			}
		});

		JobSystem::wait();
		JobSystem::flushLogs();

		// Asset loading done
		auto elapsed = engineProfiler.endTimerSec();
		fmt::print("Asset loading completed in {:.3f} seconds.\n\n", elapsed);

		// flush any setup temp data like staging buffers
		tempQueue.flush();

		// Update global descriptors
		auto& globalImgManager = ResourceManager::_globalImageManager;
		auto unifiedSet = DescriptorSetOverwatch::getUnifiedDescriptor().descriptorSet;
		DescriptorWriter mainWriter;
		mainWriter.writeFromImageLUT(_resources.getLUTManager().getEntries(), globalImgManager.table);
		mainWriter.writeImages(GLOBAL_BINDING_COMBINED_SAMPLER, DescriptorImageType::CombinedSampler, unifiedSet);
		mainWriter.updateSet(device, unifiedSet);
	}
	else {
		fmt::print("No assets for loading... skipping\n\n");
	}

	_resources.assetsLoaded = availableAssets;
}

void EngineState::initRenderer(Profiler& engineProfiler) {
	const auto device = Backend::getDevice();

	_resources.updateAddressTableMapped();

	Renderer::initRenderer(
		device,
		DescriptorSetOverwatch::getFrameDescriptor().descriptorLayout,
		_resources,
		engineProfiler
	);

	RenderScene::setScene(_resources.assetsLoaded);

	// GPU name
	// This can be defined whenever before render
	engineProfiler.getStats().gpuName = Backend::getDeviceName();

	// VRAM Usage calculator
	engineProfiler.getStats().vramStats = engineProfiler.getTotalVRAMUsage(Backend::getPhysicalDevice(), _resources.getAllocator());
}


void EngineState::renderFrame(Profiler& engineProfiler) {
	auto& frame = Renderer::getCurrentFrame();
	auto& stats = engineProfiler.getStats();
	// Every 5 seconds
	if (stats.vramQueryTimerSeconds >= 5.0f) {
		stats.vramQueryTimerSeconds -= 5.0f;

		stats.vramStats = engineProfiler.getTotalVRAMUsage(
			Backend::getPhysicalDevice(),
			_resources.getAllocator());
	}

	const auto& debug = engineProfiler.debugToggles;
	EditorImgui::renderImgui(engineProfiler);

	Renderer::prepareFrameContext(frame, _resources.getAllocator());
	if (frame.swapchainResult != VK_SUCCESS) return;

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
	const auto device = Backend::getDevice();
	const auto alloc = _resources.getAllocator();

	JobSystem::shutdownScheduler();

#ifdef TRACY_ENABLE
	Engine::getProfiler().shutdownTracyGPU();
#endif

	LightingSystem::cleanup();
	RenderScene::cleanScene();

	JobSystem::getThreadPoolManager().cleanup(device);

	for (uint32_t i = 0; i < static_cast<int>(allThreadContexts.size()); ++i) {
		ThreadContext& threadCtx = allThreadContexts[i];
		threadCtx.deletionQueue.flush();
		ASSERT(threadCtx.cmdPool == VK_NULL_HANDLE);
		ASSERT(threadCtx.lastSubmittedFence == VK_NULL_HANDLE);
		ASSERT(threadCtx.stagingMapped == nullptr);
	}

	_resources.getTempDQueue().flush();
	_resources.getMainDQueue().flush();
	_resources.getRenderTargetDQueue().flush();
	_resources.getDynamicPipelineQueue().flush();
	_resources.getDynamicPipelineShaderStagesQueue().flush();

	Renderer::cleanupRenderer(device, alloc);

	_resources.cleanup(device);
}


VkFence EngineState::submitCommandBuffers(GPUQueue& queue) {
	std::vector<VkCommandBuffer> cmds{};

	switch (queue.qType) {
	case QueueType::Graphics:
		cmds = DeferredCmdSubmitQueue::collectGraphics();
		if (cmds.empty()) {
			fmt::print("No graphics commands.\n");
			return VK_NULL_HANDLE;
		}
		break;

	case QueueType::Transfer:
		cmds = DeferredCmdSubmitQueue::collectTransfer();
		if (cmds.empty()) {
			fmt::print("No transfer commands.\n");
			return VK_NULL_HANDLE;
		}
		break;

	case QueueType::Compute:
		cmds = DeferredCmdSubmitQueue::collectCompute();
		if (cmds.empty()) {
			fmt::print("No compute commands.\n");
			return VK_NULL_HANDLE;
		}
		break;

	default:
		ASSERT(false && "Invalid queue type!");
		return VK_NULL_HANDLE;
	}

	if (cmds.empty()) {
		fmt::print("Command queue is empty for type {}.\n", static_cast<uint8_t>(queue.qType));
		return VK_NULL_HANDLE;
	}

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = static_cast<uint32_t>(cmds.size());
	submitInfo.pCommandBuffers = cmds.data();

	VkFence lastSubmittedFence = queue.submit(submitInfo);
	return lastSubmittedFence;
}
