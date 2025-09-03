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

	EditorImgui::initImgui(dQueue);

	DescriptorSetOverwatch::initDescriptors(device, dQueue);

	auto& winExtent = Engine::getWindowExtent();
	Renderer::setDrawExtent({ winExtent.width, winExtent.height, 1 });

	ResourceManager::initRenderTargets(
		device,
		_resources.getRenderTargetDQueue(),
		mainAllocator,
		Renderer::getDrawExtent());

	ResourceManager::initRenderSamplers(device, dQueue);
	ResourceManager::initShadowMapImages(device, dQueue, mainAllocator);
	ResourceManager::initTextures(device, _resources.getGraphicsPool(), dQueue, _resources.getTempDQueue(), mainAllocator);
	ResourceManager::initEnvironmentImages(device, dQueue, mainAllocator);

	RenderScene::setScene();

	PipelineManager::initPipelines(dQueue);

	// early environment compute work
	// all work is cleared afterward
	Environment::dispatchEnvironmentMaps(device, _resources, ResourceManager::_globalImageManager);

	_resources.getTempDQueue().flush();

	VK_CHECK(vkResetCommandPool(device, _resources.getGraphicsPool(), 0));
	_resources.clearLUTEntries();
	ResourceManager::_globalImageManager.clear();
}

void EngineState::loadAssets(Profiler& engineProfiler) {
	auto assetQueue = std::make_shared<GLTFAssetQueue>();

	const auto mainAllocator = _resources.getAllocator();

	EngineStages::SetGoal(ENGINE_STAGE_LOADING_START);

	bool availableAssets = false;
	// Load files for assets
	JobSystem::submitJob([assetQueue, &availableAssets](ThreadContext& threadCtx) {
		ScopedWorkQueue scoped(threadCtx, assetQueue.get());
		availableAssets = AssetManager::loadGltf(threadCtx);
		EngineStages::SetGoal(ENGINE_STAGE_LOADING_FILES_READY);
	});

	JobSystem::wait();

	const auto device = Backend::getDevice();

	if (availableAssets) {
		// main address table buffer
		_resources.getAddressTableBuffer() = BufferUtils::createBuffer(
			sizeof(GPUAddressTable),
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
			VK_BUFFER_USAGE_TRANSFER_DST_BIT |
			VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
			VMA_MEMORY_USAGE_GPU_ONLY,
			mainAllocator
		);

		fmt::println("\nAssets available for loading!");

		engineProfiler.startTimer();

		// === SAMPLER CREATION ===
		JobSystem::submitJob([assetQueue](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			AssetManager::buildSamplers(threadCtx);
			EngineStages::SetGoal(ENGINE_STAGE_LOADING_SAMPLERS_READY);
		});

		JobSystem::wait();

		// Temp queue needed for deferred buffer deletions for buffers used in commands.
		auto& tempQueue = _resources.getTempDQueue();

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

			EngineStages::SetGoal(ENGINE_STAGE_LOADING_TEXTURES_READY);
		});

		JobSystem::wait();

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

			EngineStages::SetGoal(ENGINE_STAGE_LOADING_MATERIALS_READY);
		});

		JobSystem::wait();

		// === MESH PROCESS ===
		auto& meshes = _resources.getResgisteredMeshes();
		std::vector<Vertex> totalVertices;
		std::vector<uint32_t> totalIndices;
		auto& modelDataCounts = _resources.modelDataCounts;

		JobSystem::submitJob([assetQueue, &meshes, &totalVertices, &totalIndices, &modelDataCounts](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			AssetManager::processMeshes(threadCtx, meshes, totalVertices, totalIndices, modelDataCounts);
			EngineStages::SetGoal(ENGINE_STAGE_LOADING_MESHES_READY);
		});

		JobSystem::wait();

		// Currently only scene graph and mesh upload are truly parallel

		// === MESH UPLOAD ===
		JobSystem::submitJob([assetQueue, mainAllocator, device, &meshes, &totalVertices, &totalIndices](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			threadCtx.cmdPool = JobSystem::getThreadPoolManager().getPool(threadCtx.threadID, QueueType::Transfer);
			auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
			ASSERT(queue);

			MeshLoader::uploadMeshes(
				threadCtx,
				totalVertices,
				totalIndices,
				meshes,
				mainAllocator,
				device);

			EngineStages::SetGoal(ENGINE_STAGE_MESH_UPLOAD_READY, threadCtx.threadID);
		});

		// === SCENE GRAPH BUILD ===
		JobSystem::submitJob([assetQueue, &modelDataCounts](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			SceneGraph::buildSceneGraph(
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

			EngineStages::SetGoal(ENGINE_STAGE_LOADING_SCENE_GRAPH_READY, threadCtx.threadID);
		});

		EngineStages::WaitUntilAll(ENGINE_STAGE_MESH_UPLOAD_READY | ENGINE_STAGE_LOADING_SCENE_GRAPH_READY);
		JobSystem::flushLogs();
		EngineStages::Clear(static_cast<EngineStage>(EngineStages::loadingStageFlags));

		// flush any setup temp data like staging buffers
		tempQueue.flush();

		// Asset loading done
		auto elapsed = engineProfiler.endTimerSec();
		fmt::print("Asset loading completed in {:.3f} seconds.\n\n", elapsed);
	}
	else {
		fmt::print("No assets for loading... skipping\n\n");
	}

	engineProfiler.assetsLoaded = availableAssets;

	// Define static images in global image table manager
	auto& globalImgManager = ResourceManager::_globalImageManager;

	// CSM image
	auto& shadowImg = ResourceManager::getShadowMapImage();
	shadowImg.lutEntry.combinedImageIndex = globalImgManager.addCombinedImage(shadowImg.imageView, ResourceManager::getShadowMapSampler());
	_resources.addImageLUTEntry(shadowImg.lutEntry);

	// === ENVIRONMENT IMAGE SETUP ===
	auto& skyboxImg = ResourceManager::getSkyBoxImage();
	auto skyboxSmpl = ResourceManager::getSkyBoxSampler();

	auto& diffuseImg = ResourceManager::getIrradianceImage();
	auto diffuseSmpl = ResourceManager::getIrradianceSampler();

	auto& specImg = ResourceManager::getSpecularPrefilterImage();
	auto specSmpl = ResourceManager::getSpecularPrefilterSampler();

	auto& brdfImg = ResourceManager::getBRDFImage();
	auto brdfSmpl = ResourceManager::getBRDFSampler();

	// In current implementation the entries must be pushed in proper order
	// Diffuse -> Specular -> BRDF -> Skybox
	// Uniforms are strict and environment images work in sets of 4
	std::vector<ImageLUTEntry> tempEnvMapIdx;
	diffuseImg.lutEntry.samplerCubeIndex = globalImgManager.addCubeImage(diffuseImg.imageView, diffuseSmpl);
	tempEnvMapIdx.push_back(diffuseImg.lutEntry);
	_resources.addImageLUTEntry(diffuseImg.lutEntry);

	specImg.lutEntry.samplerCubeIndex = globalImgManager.addCubeImage(specImg.imageView, specSmpl);
	tempEnvMapIdx.push_back(specImg.lutEntry);
	_resources.addImageLUTEntry(specImg.lutEntry);

	brdfImg.lutEntry.combinedImageIndex = globalImgManager.addCombinedImage(brdfImg.imageView, brdfSmpl);
	tempEnvMapIdx.push_back(brdfImg.lutEntry);
	_resources.addImageLUTEntry(brdfImg.lutEntry);

	skyboxImg.lutEntry.samplerCubeIndex = globalImgManager.addCubeImage(skyboxImg.imageView, skyboxSmpl);
	tempEnvMapIdx.push_back(skyboxImg.lutEntry);
	_resources.addImageLUTEntry(skyboxImg.lutEntry);

	ASSERT(tempEnvMapIdx.size() % 4 == 0 && "Environment LUT entries must be in sets of 4");

	uint32_t setIndex = 0;
	for (size_t i = 0; i < tempEnvMapIdx.size(); i += 4) {
		const ImageLUTEntry& diffuse = tempEnvMapIdx[i];
		const ImageLUTEntry& specular = tempEnvMapIdx[i + 1];
		const ImageLUTEntry& brdf = tempEnvMapIdx[i + 2];
		const ImageLUTEntry& skybox = tempEnvMapIdx[i + 3];

		ASSERT(diffuse.samplerCubeIndex != UINT32_MAX);
		ASSERT(specular.samplerCubeIndex != UINT32_MAX);
		ASSERT(brdf.combinedImageIndex != UINT32_MAX);
		ASSERT(skybox.samplerCubeIndex != UINT32_MAX);

		glm::uvec4 envEntry{};
		envEntry.x = diffuse.samplerCubeIndex;
		envEntry.y = specular.samplerCubeIndex;
		envEntry.z = brdf.combinedImageIndex;
		envEntry.w = skybox.samplerCubeIndex;

		ASSERT(setIndex < MAX_ENV_SETS && "Too many environment sets for fixed UBO buffer!");
		ResourceManager::_envMapIdxArray.indices[setIndex++] = envEntry;
	}

	_resources.envMapIndexBuffer = BufferUtils::createUniformBuffer(ResourceManager::_envMapIdxArray, mainAllocator);

	// ssao kernel block
	ResourceManager::initSSAOKernel();
	_resources.ssaoKernelBuffer = BufferUtils::createUniformBuffer(ResourceManager::_ssaoKernelBlock, mainAllocator);

	// Global descriptor writing and update
	auto unifiedSet = DescriptorSetOverwatch::getUnifiedDescriptor().descriptorSet;
	DescriptorWriter mainWriter;
	if (availableAssets) {
		mainWriter.writeBuffer(
			ADDRESS_TABLE_BINDING,
			_resources.getAddressTableBuffer().buffer,
			sizeof(GPUAddressTable),
			0,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			unifiedSet);
	}
	// env map index
	mainWriter.writeBuffer(
		GLOBAL_BINDING_ENV_INDEX,
		_resources.envMapIndexBuffer.buffer,
		sizeof(GPUEnvMapIndexArray),
		0,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		unifiedSet);

	// ssao kernel UBO
	mainWriter.writeBuffer(
		GLOBAL_BINDING_SSAO_KERNEL,
		_resources.ssaoKernelBuffer.buffer,
		sizeof(glm::vec4[ResourceManager::_kernelBlockSize]),
		0,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		unifiedSet);

	mainWriter.writeFromImageLUT(_resources.getLUTManager().getEntries(), globalImgManager.table);
	mainWriter.writeImages(GLOBAL_BINDING_SAMPLER_CUBE, DescriptorImageType::SamplerCube, unifiedSet);
	mainWriter.writeImages(GLOBAL_BINDING_STORAGE_IMAGE, DescriptorImageType::StorageImage, unifiedSet);
	mainWriter.writeImages(GLOBAL_BINDING_COMBINED_SAMPLER, DescriptorImageType::CombinedSampler, unifiedSet);
	mainWriter.updateSet(device, unifiedSet);

	EngineStages::SetGoal(ENGINE_STAGE_READY);
}

void EngineState::initRenderer(Profiler& engineProfiler) {
	const auto device = Backend::getDevice();

	Renderer::initRenderer(
		device,
		DescriptorSetOverwatch::getFrameDescriptor().descriptorLayout,
		_resources,
		engineProfiler
	);

	// GPU name
	// This can be defined whenever before render
	engineProfiler.getStats().gpuName = Backend::getDeviceName();

	// VRAM Usage calculator
	auto physicalDevice = Backend::getPhysicalDevice();
	engineProfiler.getStats().vramStats = engineProfiler.GetTotalVRAMUsage(physicalDevice, _resources.getAllocator());
}


void EngineState::renderFrame(Profiler& engineProfiler) {
	auto& frame = Renderer::getCurrentFrame();

	const auto& debug = engineProfiler.debugToggles;
	if (debug.enableSettings || debug.enableStats)
		EditorImgui::renderImgui(engineProfiler);

	Renderer::prepareFrameContext(frame);
	if (frame.swapchainResult != VK_SUCCESS) return;

	engineProfiler.resetRenderTimers();
	engineProfiler.resetDrawCalls();

	engineProfiler.startTimer();
	RenderScene::updateScene(frame, _resources, debug);
	auto elapsed = engineProfiler.endTimerMS();
	engineProfiler.getStats().sceneUpdateTime = elapsed;

	engineProfiler.startTimer();
	Renderer::recordRenderCommand(frame, engineProfiler);
	elapsed = engineProfiler.endTimerMS();
	engineProfiler.getStats().drawTime = elapsed;

	Renderer::submitFrame(frame, _resources);
}

void EngineState::shutdown() {
	const auto device = Backend::getDevice();

	JobSystem::shutdownScheduler();

	if (!RenderScene::_loadedScenes.empty())
		RenderScene::cleanScene();

	JobSystem::getThreadPoolManager().cleanup(device);

	for (int i = 0; i < static_cast<int>(allThreadContexts.size()); ++i) {
		ThreadContext& threadCtx = allThreadContexts[i];
		threadCtx.deletionQueue.flush();
		ASSERT(threadCtx.cmdPool == VK_NULL_HANDLE);
		ASSERT(threadCtx.lastSubmittedFence == VK_NULL_HANDLE);
		ASSERT(threadCtx.stagingMapped == nullptr);
	}

	_resources.getTempDQueue().flush();
	_resources.getMainDQueue().flush();
	_resources.getRenderTargetDQueue().flush();

	Renderer::cleanupRenderer(device, _resources.getAllocator());

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