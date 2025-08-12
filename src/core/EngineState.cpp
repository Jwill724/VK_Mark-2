#include "pch.h"

#include "EngineState.h"
#include "renderer/gpu_types/CommandBuffer.h"
#include "renderer/gpu_types/Descriptor.h"
#include "renderer/renderer.h"
#include "utils/VulkanUtils.h"
#include "utils/BufferUtils.h"
#include "AssetManager.h"
#include "Environment.h"
#include "JobSystem.h"
#include "renderer/RenderScene.h"
#include "renderer/SceneGraph.h"
#include "profiler/EditorImgui.h"
#include "Engine.h"

std::vector<ThreadContext>& allThreadContexts = getAllThreadContexts();

void EngineState::init() {
	auto device = Backend::getDevice();

	JobSystem::initScheduler();

	uint32_t graphicsIndex = Backend::getGraphicsQueue().familyIndex;
	uint32_t transferIndex = Backend::getTransferQueue().familyIndex;

	JobSystem::getThreadPoolManager().init(device, static_cast<uint32_t>(allThreadContexts.size()), graphicsIndex, transferIndex);

	_resources.init(device);
	auto& dQueue = _resources.getMainDeletionQueue();
	auto mainAllocator = _resources.getAllocator();

	EditorImgui::initImgui(dQueue);
	DescriptorSetOverwatch::initDescriptors(dQueue);

	auto& winExtent = Engine::getWindowExtent();
	Renderer::setDrawExtent({ winExtent.width, winExtent.height, 1 });
	ResourceManager::initRenderImages(dQueue, mainAllocator, Renderer::getDrawExtent());
	ResourceManager::initTextures(_resources.getGraphicsPool(), dQueue, _resources.getTempDeletionQueue(), mainAllocator);

	ResourceManager::initEnvironmentImages(dQueue, mainAllocator);

	// main address table buffer
	_resources.getAddressTableBuffer() = BufferUtils::createBuffer(
		sizeof(GPUAddressTable),
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
		VK_BUFFER_USAGE_TRANSFER_DST_BIT |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_GPU_ONLY,
		mainAllocator
	);

	RenderScene::setScene();

	PipelineManager::initPipelines(dQueue);

	// early environment compute work
	// all work is cleared afterward
	Environment::dispatchEnvironmentMaps(_resources, ResourceManager::_globalImageManager);

	_resources.getTempDeletionQueue().flush();

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

	if (availableAssets) {
		fmt::print("\nAssets available for loading!\n");

		engineProfiler.startTimer();

		JobSystem::submitJob([assetQueue](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			AssetManager::buildSamplers(threadCtx);
			EngineStages::SetGoal(ENGINE_STAGE_LOADING_SAMPLERS_READY);
		});

		JobSystem::wait();

		// temp queue needed for deferred buffer deletions for buffers used in commands
		auto& tempQueue = _resources.getTempDeletionQueue();
		JobSystem::submitJob([assetQueue, mainAllocator, &tempQueue](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			threadCtx.cmdPool = JobSystem::getThreadPoolManager().getPool(threadCtx.threadID, QueueType::Graphics);
			AssetManager::decodeImages(threadCtx, mainAllocator, tempQueue);
			auto& gQueue = Backend::getGraphicsQueue();
			auto device = Backend::getDevice();

			threadCtx.lastSubmittedFence = Engine::getState().submitCommandBuffers(gQueue);
			waitAndRecycleLastFence(threadCtx.lastSubmittedFence, gQueue, device);
			vkResetCommandPool(device, threadCtx.cmdPool, 0);
			threadCtx.cmdPool = VK_NULL_HANDLE;

			EngineStages::SetGoal(ENGINE_STAGE_LOADING_TEXTURES_READY);
		});

		JobSystem::wait();

		JobSystem::submitJob([assetQueue, mainAllocator](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			threadCtx.cmdPool = JobSystem::getThreadPoolManager().getPool(threadCtx.threadID, QueueType::Transfer);
			AssetManager::processMaterials(threadCtx, mainAllocator);

			auto device = Backend::getDevice();
			auto& tQueue = Backend::getTransferQueue();

			threadCtx.lastSubmittedFence = Engine::getState().submitCommandBuffers(tQueue);
			waitAndRecycleLastFence(threadCtx.lastSubmittedFence, tQueue, device);
			vkResetCommandPool(device, threadCtx.cmdPool, 0);
			threadCtx.cmdPool = VK_NULL_HANDLE;

			EngineStages::SetGoal(ENGINE_STAGE_LOADING_MATERIALS_READY);
		});

		JobSystem::wait();
		// clear material staging buffer
		_resources.getTempDeletionQueue().flush();

		auto& drawRanges = _resources.getDrawRanges();
		auto& meshes = _resources.getResgisteredMeshes();

		std::vector<Vertex> totalVertices;
		std::vector<uint32_t> totalIndices;

		JobSystem::submitJob([assetQueue, &drawRanges, &meshes, &totalVertices, &totalIndices](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			AssetManager::processMeshes(threadCtx, drawRanges, meshes, totalVertices, totalIndices);
			EngineStages::SetGoal(ENGINE_STAGE_LOADING_MESHES_READY);
		});

		JobSystem::wait();

		// Mesh upload
		JobSystem::submitJob([assetQueue, mainAllocator, &drawRanges, &meshes, &totalVertices, &totalIndices](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			threadCtx.cmdPool = JobSystem::getThreadPoolManager().getPool(threadCtx.threadID, QueueType::Transfer);
			auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
			ASSERT(queue);

			const size_t vertexBufferSize = totalVertices.size() * sizeof(Vertex);
			const size_t indexBufferSize = totalIndices.size() * sizeof(uint32_t);
			const size_t drawRangesSize = drawRanges.size() * sizeof(GPUDrawRange);
			const size_t meshesSize = meshes.meshData.size() * sizeof(GPUMeshData);
			const size_t totalStagingSize = vertexBufferSize + indexBufferSize + drawRangesSize + meshesSize;

			fmt::print("[MeshUpload] vertexBufferSize   = {} bytes ({} vertices)\n",
				vertexBufferSize, totalVertices.size());
			fmt::print("[MeshUpload] indexBufferSize    = {} bytes ({} indices)\n",
				indexBufferSize, totalIndices.size());
			fmt::print("[MeshUpload] drawRangesSize     = {} bytes ({} ranges)\n",
				drawRangesSize, drawRanges.size());
			fmt::print("[MeshUpload] meshesSize         = {} bytes ({} meshes)\n",
				meshesSize, meshes.meshData.size());
			fmt::print("[MeshUpload] totalStagingSize   = {} bytes\n", totalStagingSize);

			auto& resources = Engine::getState().getGPUResources();

			// Create large GPU buffers for vertex and index
			AllocatedBuffer vtxBuffer = BufferUtils::createGPUAddressBuffer(
				AddressBufferType::Vertex,
				resources.getAddressTable(),
				vertexBufferSize,
				mainAllocator
			);
			resources.addGPUBufferToGlobalAddress(AddressBufferType::Vertex, vtxBuffer);

			AllocatedBuffer idxBuffer = BufferUtils::createGPUAddressBuffer(
				AddressBufferType::Index,
				resources.getAddressTable(),
				indexBufferSize,
				mainAllocator
			);
			resources.addGPUBufferToGlobalAddress(AddressBufferType::Index, idxBuffer);

			// Draw range gpu buffer creation
			AllocatedBuffer drawRangeBuffer = BufferUtils::createGPUAddressBuffer(
				AddressBufferType::DrawRange,
				resources.getAddressTable(),
				drawRangesSize,
				mainAllocator
			);
			resources.addGPUBufferToGlobalAddress(AddressBufferType::DrawRange, drawRangeBuffer);

			// Mesh buffer creation
			AllocatedBuffer meshBuffer = BufferUtils::createGPUAddressBuffer(
				AddressBufferType::Mesh,
				resources.getAddressTable(),
				meshesSize,
				mainAllocator
			);
			resources.addGPUBufferToGlobalAddress(AddressBufferType::Mesh, meshBuffer);

			// Setup single staging buffer for transfer
			AllocatedBuffer stagingBuffer = BufferUtils::createBuffer(
				totalStagingSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
				mainAllocator
			);
			ASSERT(stagingBuffer.info.size >= totalStagingSize);

			auto stgBuf = stagingBuffer.buffer;
			auto stgAlloc = stagingBuffer.allocation;
			resources.getTempDeletionQueue().push_function([stgBuf, stgAlloc, mainAllocator]() mutable {
				BufferUtils::destroyBuffer(stgBuf, stgAlloc, mainAllocator);
			});

			threadCtx.stagingMapped = stagingBuffer.info.pMappedData;
			ASSERT(threadCtx.stagingMapped != nullptr);
			uint8_t* stagingData = reinterpret_cast<uint8_t*>(threadCtx.stagingMapped);

			// Compute offsets
			VkDeviceSize vertexWriteOffset = 0;
			VkDeviceSize indexWriteOffset = vertexWriteOffset + vertexBufferSize;
			VkDeviceSize drawRangesWriteOffset = indexWriteOffset + indexBufferSize;
			VkDeviceSize meshesWriteOffset = drawRangesWriteOffset + drawRangesSize;

			fmt::print("[MeshUpload] vertexWriteOffset     = {}\n", vertexWriteOffset);
			fmt::print("[MeshUpload] indexWriteOffset      = {}\n", indexWriteOffset);
			fmt::print("[MeshUpload] drawRangesWriteOffset = {}\n", drawRangesWriteOffset);
			fmt::print("[MeshUpload] meshesWriteOffset     = {}\n", meshesWriteOffset);

			// Copy into staging
			memcpy(stagingData + vertexWriteOffset, totalVertices.data(), vertexBufferSize);
			memcpy(stagingData + indexWriteOffset, totalIndices.data(), indexBufferSize);
			memcpy(stagingData + drawRangesWriteOffset, drawRanges.data(), drawRangesSize);
			memcpy(stagingData + meshesWriteOffset, meshes.meshData.data(), meshesSize);

			CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {
				VkBufferCopy vtxCopy{
					.srcOffset = vertexWriteOffset,
					.dstOffset = 0,
					.size = vertexBufferSize
				};
				vkCmdCopyBuffer(cmd, stagingBuffer.buffer, vtxBuffer.buffer, 1, &vtxCopy);

				VkBufferCopy idxCopy{
					.srcOffset = indexWriteOffset,
					.dstOffset = 0,
					.size = indexBufferSize
				};
				vkCmdCopyBuffer(cmd, stagingBuffer.buffer, idxBuffer.buffer, 1, &idxCopy);

				VkBufferCopy drawRangeCopy{
					.srcOffset = drawRangesWriteOffset,
					.dstOffset = 0,
					.size = drawRangesSize
				};
				vkCmdCopyBuffer(cmd, stagingBuffer.buffer, drawRangeBuffer.buffer, 1, &drawRangeCopy);

				VkBufferCopy meshCopy{
					.srcOffset = meshesWriteOffset,
					.dstOffset = 0,
					.size = meshesSize
				};
				vkCmdCopyBuffer(cmd, stagingBuffer.buffer, meshBuffer.buffer, 1, &meshCopy);

			}, threadCtx.cmdPool, QueueType::Transfer);

			resources.updateAddressTableMapped(threadCtx.cmdPool);

			auto& tQueue = Backend::getTransferQueue();
			auto device = Backend::getDevice();
			threadCtx.lastSubmittedFence = Engine::getState().submitCommandBuffers(tQueue);
			waitAndRecycleLastFence(threadCtx.lastSubmittedFence, tQueue, device);
			vkResetCommandPool(device, threadCtx.cmdPool, 0);
			threadCtx.cmdPool = VK_NULL_HANDLE;
			threadCtx.stagingMapped = nullptr;
		});

		JobSystem::wait();
		_resources.getTempDeletionQueue().flush();

		JobSystem::submitJob([assetQueue, &meshes](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			SceneGraph::buildSceneGraph(threadCtx, meshes.meshData);

			auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
			ASSERT(queue && "queue broke.");

			auto gltfJobs = queue->collect();

			for (auto& context : gltfJobs) {
				if (!context->isComplete()) continue;
				auto& scene = *context->scene;
				const std::string& name = scene.sceneName;

				if (!context->hasRegisteredScene) {
					RenderScene::_loadedScenes[name] = context->scene;
					fmt::print("Registered scene '{}'\n", name);
					context->hasRegisteredScene = true;
				}
			}

			EngineStages::SetGoal(ENGINE_STAGE_LOADING_SCENE_GRAPH_READY);
		});

		JobSystem::wait();
		EngineStages::Clear(static_cast<EngineStage>(EngineStages::loadingStageFlags));

		// Asset loading done
		auto elapsed = engineProfiler.endTimer();
		fmt::print("Asset loading completed in {:.3f} seconds.\n", elapsed);
	}
	else {
		fmt::print("No assets for loading... skipping\n");
	}

	engineProfiler.assetsLoaded = availableAssets;

	// Define static images in global image table manager
	auto& globalImgManager = ResourceManager::_globalImageManager;

	auto& toneMapImg = ResourceManager::getToneMappingImage();
	auto& drawImg = ResourceManager::getDrawImage();
	toneMapImg.lutEntry.storageImageIndex = globalImgManager.addStorageImage(toneMapImg.storageView);
	drawImg.lutEntry.combinedImageIndex = globalImgManager.addCombinedImage(drawImg.imageView, ResourceManager::getDefaultSamplerLinear());
	_resources.addImageLUTEntry(toneMapImg.lutEntry);
	_resources.addImageLUTEntry(drawImg.lutEntry);

	ResourceManager::toneMappingData.brightness = 1.0f;
	ResourceManager::toneMappingData.saturation = 1.0f;
	ResourceManager::toneMappingData.contrast = 1.0f;
	ResourceManager::toneMappingData.cmbViewIdx = drawImg.lutEntry.combinedImageIndex;
	ResourceManager::toneMappingData.storageViewIdx = toneMapImg.lutEntry.storageImageIndex;

	// === ENVIRONMENT IMAGE SETUP ===
	auto& skyboxImg = ResourceManager::getSkyBoxImage();
	auto& skyboxSmpl = ResourceManager::getSkyBoxSampler();

	auto& diffuseImg = ResourceManager::getIrradianceImage();
	auto& diffuseSmpl = ResourceManager::getIrradianceSampler();

	auto& specImg = ResourceManager::getSpecularPrefilterImage();
	auto& specSmpl = ResourceManager::getSpecularPrefilterSampler();

	auto& brdfImg = ResourceManager::getBRDFImage();
	auto& brdfSmpl = ResourceManager::getBRDFSampler();

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

	_resources.envMapIndexBuffer = BufferUtils::createBuffer(sizeof(GPUEnvMapIndexArray),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, mainAllocator);

	GPUEnvMapIndexArray* envMapIndices = reinterpret_cast<GPUEnvMapIndexArray*>(_resources.envMapIndexBuffer.mapped);
	*envMapIndices = ResourceManager::_envMapIdxArray;
	vmaFlushAllocation(mainAllocator, _resources.envMapIndexBuffer.allocation, 0, VK_WHOLE_SIZE);

	// Global descriptor writing and update
	auto unifiedSet = DescriptorSetOverwatch::getUnifiedDescriptors().descriptorSet;
	DescriptorWriter mainWriter;
	mainWriter.writeBuffer(
		ADDRESS_TABLE_BINDING,
		_resources.getAddressTableBuffer().buffer,
		_resources.getAddressTableBuffer().info.size,
		0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		unifiedSet);
	mainWriter.writeBuffer(
		GLOBAL_BINDING_ENV_INDEX,
		_resources.envMapIndexBuffer.buffer,
		sizeof(GPUEnvMapIndexArray),
		0,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		unifiedSet);

	mainWriter.writeFromImageLUT(_resources.getLUTManager().getEntries(), globalImgManager.table);
	mainWriter.writeImages(GLOBAL_BINDING_SAMPLER_CUBE, DescriptorImageType::SamplerCube, unifiedSet);
	mainWriter.writeImages(GLOBAL_BINDING_STORAGE_IMAGE, DescriptorImageType::StorageImage, unifiedSet);
	mainWriter.writeImages(GLOBAL_BINDING_COMBINED_SAMPLER, DescriptorImageType::CombinedSampler, unifiedSet);
	mainWriter.updateSet(Backend::getDevice(), unifiedSet);

	EngineStages::SetGoal(ENGINE_STAGE_READY);
}

void EngineState::initRenderer(Profiler& engineProfiler) {
	auto device = Backend::getDevice();

	Renderer::initFrameContexts(
		device,
		DescriptorSetOverwatch::getFrameDescriptors().descriptorLayout,
		_resources,
		engineProfiler.assetsLoaded
	);

	// VRAM Usage calculator
	auto physicalDevice = Backend::getPhysicalDevice();
	VkDeviceSize totalUsedVRAM = 0;

	totalUsedVRAM += engineProfiler.GetTotalVRAMUsage(physicalDevice, _resources.getAllocator());
	engineProfiler.getStats().vramUsed = totalUsedVRAM;
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
	RenderScene::updateScene(frame, _resources);
	auto elapsed = engineProfiler.endTimer();
	engineProfiler.getStats().sceneUpdateTime = elapsed;

	engineProfiler.startTimer();
	Renderer::recordRenderCommand(frame, engineProfiler);
	elapsed = engineProfiler.endTimer();
	engineProfiler.getStats().drawTime = elapsed;

	Renderer::submitFrame(frame);
}

void EngineState::shutdown() {
	auto device = Backend::getDevice();

	JobSystem::shutdownScheduler();

	if (!RenderScene::_loadedScenes.empty())
		RenderScene::_loadedScenes.clear();

	JobSystem::getThreadPoolManager().cleanup(device);

	for (int i = 0; i < static_cast<int>(allThreadContexts.size()); ++i) {
		ThreadContext& threadCtx = allThreadContexts[i];
		threadCtx.deletionQueue.flush();
		ASSERT(threadCtx.cmdPool == VK_NULL_HANDLE);
		ASSERT(threadCtx.lastSubmittedFence == VK_NULL_HANDLE);
		ASSERT(threadCtx.stagingMapped == nullptr);
	}

	_resources.getTempDeletionQueue().flush();
	_resources.getMainDeletionQueue().flush();

	Renderer::cleanup();

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