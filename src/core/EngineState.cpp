#include "pch.h"

#include "EngineState.h"
#include "common/Vk_Types.h"
#include "common/EngineConstants.h"
#include "renderer/gpu_types/CommandBuffer.h"
#include "renderer/gpu_types/Descriptor.h"
#include "renderer/renderer.h"
#include "utils/VulkanUtils.h"
#include "AssetManager.h"
#include "Environment.h"
#include "JobSystem.h"
#include "renderer/RenderScene.h"
#include "renderer/SceneGraph.h"
#include "renderer/DrawPreperation.h"
#include "profiler/EditorImgui.h"
#include "Engine.h"

std::vector<ThreadContext>& allThreadContexts = getAllThreadContexts();

void EngineState::init() {
	auto device = Backend::getDevice();

	JobSystem::initScheduler();

	uint32_t graphicsIndex = Backend::getGraphicsQueue().familyIndex;
	uint32_t transferIndex = Backend::getTransferQueue().familyIndex;
	uint32_t computeIndex = Backend::getComputeQueue().familyIndex;
	JobSystem::getThreadPoolManager().init(device, static_cast<uint32_t>(allThreadContexts.size()), graphicsIndex, transferIndex);

	_resources.init();
	auto& dQueue = _resources.getMainDeletionQueue();
	auto mainAllocator = _resources.getAllocator();

	EditorImgui::initImgui(dQueue);
	DescriptorSetOverwatch::initDescriptors(dQueue);

	auto frameLayout = DescriptorSetOverwatch::getFrameDescriptors().descriptorLayout;
	Renderer::initFrameContexts(device, graphicsIndex, transferIndex, computeIndex, Engine::getWindowExtent(), frameLayout, mainAllocator);
	ResourceManager::initRenderImages(dQueue, mainAllocator);
	ResourceManager::initTextures(_resources.getGraphicsPool(), dQueue, _resources.getTempDeletionQueue(), mainAllocator);

	ResourceManager::initEnvironmentImages(dQueue, mainAllocator);

	// address table buffer
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
	Environment::dispatchEnvironmentMaps(_resources, ResourceManager::_globalImageTable);
	_resources.getTempDeletionQueue().flush();
	VK_CHECK(vkResetCommandPool(device, _resources.getGraphicsPool(), 0));
	_resources.clearLUTEntries();
	ResourceManager::_globalImageTable.clearTables();
}

void EngineState::loadAssets() {
	auto assetQueue = std::make_shared<GLTFAssetQueue>();

	auto mainAllocator = _resources.getAllocator();

	EngineStages::SetGoal(ENGINE_STAGE_LOADING_START);

	bool availableAssets = false;
	// Load files for assets
	JobSystem::submitJob([assetQueue, &availableAssets](ThreadContext& threadCtx) {
		ScopedWorkQueue scoped(threadCtx, assetQueue.get());
		availableAssets = AssetManager::loadGltf(threadCtx);
		EngineStages::SetGoal(ENGINE_STAGE_LOADING_FILES_READY);
	});

	JobSystem::wait();

	auto& profiler = Engine::getProfiler();

	if (availableAssets) {
		fmt::print("Assets available for loading!\n");

		profiler.startTimer();

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

		JobSystem::submitJob([assetQueue, &drawRanges, &meshes](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			AssetManager::processMeshes(threadCtx, drawRanges, meshes);
			EngineStages::SetGoal(ENGINE_STAGE_LOADING_MESHES_READY);
		});

		JobSystem::wait();

		// Mesh buffer prep
		JobSystem::submitJob([assetQueue, &drawRanges, &meshes](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
			ASSERT(queue);

			auto gltfJobs = queue->collect();

			for (auto& context : gltfJobs) {
				if (!context->isJobComplete(GLTFJobType::ProcessMeshes)) continue;

				auto& uploadCtx = context->uploadMeshCtx;
				uploadCtx.meshViews.reserve(meshes.meshData.size());

				for (auto& mesh : meshes.meshData) {
					ASSERT(mesh.drawRangeIndex < drawRanges.size());
					const GPUDrawRange& range = drawRanges[mesh.drawRangeIndex];

					UploadedMeshView newView{};
					newView.vertexSizeBytes = range.vertexCount * sizeof(Vertex);
					newView.indexSizeBytes = range.indexCount * sizeof(uint32_t);

					uploadCtx.meshViews.push_back(newView);
				}

				queue->push(context);
				context->markJobComplete(GLTFJobType::MeshBufferReady);
			}

			EngineStages::SetGoal(ENGINE_STAGE_LOADING_UPLOAD_MESH_DATA);
		});

		JobSystem::wait();

		// Mesh upload
		JobSystem::submitJob([assetQueue, mainAllocator, &drawRanges, &meshes](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			threadCtx.cmdPool = JobSystem::getThreadPoolManager().getPool(threadCtx.threadID, QueueType::Transfer);
			auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
			ASSERT(queue);

			auto gltfJobs = queue->collect();

			// Calculate buffer sizes and vertex/index counts
			size_t vertexBufferSize = 0;
			size_t indexBufferSize = 0;
			for (auto& context : gltfJobs) {
				for (auto& meshView : context->uploadMeshCtx.meshViews) {
					vertexBufferSize += meshView.vertexSizeBytes;
					indexBufferSize += meshView.indexSizeBytes;
				}
			}

			const size_t drawRangesSize = drawRanges.size() * sizeof(GPUDrawRange);
			const size_t meshesSize = meshes.meshData.size() * sizeof(GPUMeshData);

			auto& resources = Engine::getState().getGPUResources();

			// Create large GPU buffers for vertex and index
			AllocatedBuffer vtxBuffer = BufferUtils::createGPUAddressBuffer(
				AddressBufferType::Vertex,
				resources.getAddressTable(),
				vertexBufferSize,
				mainAllocator
			);
			resources.addGPUBuffer(AddressBufferType::Vertex, vtxBuffer);

			AllocatedBuffer idxBuffer = BufferUtils::createGPUAddressBuffer(
				AddressBufferType::Index,
				resources.getAddressTable(),
				indexBufferSize,
				mainAllocator
			);
			resources.addGPUBuffer(AddressBufferType::Index, idxBuffer);

			// Draw range gpu buffer creation
			AllocatedBuffer drawRangeBuffer = BufferUtils::createGPUAddressBuffer(
				AddressBufferType::DrawRange,
				resources.getAddressTable(),
				drawRangesSize,
				mainAllocator);
			resources.addGPUBuffer(AddressBufferType::DrawRange, drawRangeBuffer);

			// Mesh buffer creation
			AllocatedBuffer meshBuffer = BufferUtils::createGPUAddressBuffer(
				AddressBufferType::Mesh,
				resources.getAddressTable(),
				meshesSize,
				mainAllocator);
			resources.addGPUBuffer(AddressBufferType::Mesh, meshBuffer);

			// Setup single staging buffer for transfer
			threadCtx.stagingBuffer = BufferUtils::createBuffer(
				vertexBufferSize + indexBufferSize + drawRangesSize + meshesSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
				mainAllocator
			);
			ASSERT(threadCtx.stagingBuffer.info.size >= vertexBufferSize + indexBufferSize + drawRangesSize + meshesSize);

			auto buffer = threadCtx.stagingBuffer;
			resources.getTempDeletionQueue().push_function([buffer, mainAllocator]() mutable {
				BufferUtils::destroyBuffer(buffer, mainAllocator);
			});
			threadCtx.stagingMapped = threadCtx.stagingBuffer.info.pMappedData;
			ASSERT(threadCtx.stagingMapped != nullptr);

			uint8_t* stagingData = reinterpret_cast<uint8_t*>(threadCtx.stagingMapped);

			VkDeviceSize vertexWriteOffset = 0;
			VkDeviceSize indexWriteOffset = vertexWriteOffset + vertexBufferSize;
			VkDeviceSize drawRangesWriteOffset = indexWriteOffset + indexBufferSize;
			VkDeviceSize meshesWriteOffset = drawRangesWriteOffset + drawRangesSize;

			// Copy drawRanges to staging
			memcpy(stagingData + drawRangesWriteOffset, drawRanges.data(), drawRangesSize);
			// Copy mesh data to staging
			memcpy(stagingData + meshesWriteOffset, meshes.meshData.data(), meshesSize);

			CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {
				for (auto& context : gltfJobs) {
					if (!context->isComplete()) continue;

					auto& meshCtx = context->uploadMeshCtx;

					// Copy all vertex data into staging
					memcpy(stagingData + vertexWriteOffset,
						meshCtx.globalVertices.data(),
						meshCtx.globalVertices.size() * sizeof(Vertex));
					// Copy all index data into staging
					memcpy(stagingData + indexWriteOffset,
						meshCtx.globalIndices.data(),
						meshCtx.globalIndices.size() * sizeof(uint32_t));

					queue->push(context);
				}

				VkBufferCopy vtxCopy {
					.srcOffset = vertexWriteOffset,
					.dstOffset = 0,
					.size = vertexBufferSize
				};
				vkCmdCopyBuffer(cmd, threadCtx.stagingBuffer.buffer, vtxBuffer.buffer, 1, &vtxCopy);

				VkBufferCopy idxCopy {
					.srcOffset = indexWriteOffset,
					.dstOffset = 0,
					.size = indexBufferSize
				};
				vkCmdCopyBuffer(cmd, threadCtx.stagingBuffer.buffer, idxBuffer.buffer, 1, &idxCopy);

				VkBufferCopy drawRangeCopy {
					.srcOffset = drawRangesWriteOffset,
					.dstOffset = 0,
					.size = drawRangesSize
				};
				vkCmdCopyBuffer(cmd, threadCtx.stagingBuffer.buffer, drawRangeBuffer.buffer, 1, &drawRangeCopy);

				VkBufferCopy meshCopy {
					.srcOffset = meshesWriteOffset,
					.dstOffset = 0,
					.size = meshesSize
				};
				vkCmdCopyBuffer(cmd, threadCtx.stagingBuffer.buffer, meshBuffer.buffer, 1, &meshCopy);

			}, threadCtx.cmdPool, QueueType::Transfer);

			resources.updateAddressTableMapped(threadCtx.cmdPool);

			auto& tQueue = Backend::getTransferQueue();
			auto device = Backend::getDevice();
			threadCtx.lastSubmittedFence = Engine::getState().submitCommandBuffers(tQueue);
			waitAndRecycleLastFence(threadCtx.lastSubmittedFence, tQueue, device);
			vkResetCommandPool(device, threadCtx.cmdPool, 0);
			threadCtx.cmdPool = VK_NULL_HANDLE;
			threadCtx.stagingMapped = nullptr;
			threadCtx.stagingBuffer.buffer = VK_NULL_HANDLE;
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
		auto elapsed = profiler.endTimer();
		fmt::print("Asset loading completed in {:.3f} seconds.\n", elapsed);
	}
	else {
		fmt::print("No assets for loading... skipping\n");
	}

	// Perma imagelut work starts here
	auto& postProcessImg = ResourceManager::getPostProcessImage();
	auto& drawImg = ResourceManager::getDrawImage();
	postProcessImg.lutEntry.storageImageIndex = ResourceManager::_globalImageTable.pushStorage(postProcessImg.storageView);
	postProcessImg.lutEntry.combinedImageIndex = ResourceManager::_globalImageTable.pushCombined(drawImg.imageView,
		ResourceManager::getDefaultSamplerLinear());
	_resources.addImageLUTEntry(postProcessImg.lutEntry);

	ResourceManager::toneMappingData.brightness = 1.0f;
	ResourceManager::toneMappingData.saturation = 1.0f;
	ResourceManager::toneMappingData.contrast = 1.0f;
	ResourceManager::toneMappingData.cmbViewIdx = postProcessImg.lutEntry.combinedImageIndex;
	ResourceManager::toneMappingData.storageViewIdx = postProcessImg.lutEntry.storageImageIndex;

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
	diffuseImg.lutEntry.samplerCubeIndex = ResourceManager::_globalImageTable.pushSamplerCube(diffuseImg.imageView, diffuseSmpl);
	tempEnvMapIdx.push_back(diffuseImg.lutEntry);
	_resources.addImageLUTEntry(diffuseImg.lutEntry);

	specImg.lutEntry.samplerCubeIndex = ResourceManager::_globalImageTable.pushSamplerCube(specImg.imageView, specSmpl);
	tempEnvMapIdx.push_back(specImg.lutEntry);
	_resources.addImageLUTEntry(specImg.lutEntry);

	brdfImg.lutEntry.combinedImageIndex = ResourceManager::_globalImageTable.pushCombined(brdfImg.imageView, brdfSmpl);
	tempEnvMapIdx.push_back(brdfImg.lutEntry);
	_resources.addImageLUTEntry(brdfImg.lutEntry);

	skyboxImg.lutEntry.samplerCubeIndex = ResourceManager::_globalImageTable.pushSamplerCube(skyboxImg.imageView, skyboxSmpl);
	tempEnvMapIdx.push_back(skyboxImg.lutEntry);
	_resources.addImageLUTEntry(skyboxImg.lutEntry);

	ASSERT(tempEnvMapIdx.size() % 4 == 0 && "Environment LUT entries must be in sets of 4");

	size_t setIndex = 0;
	for (size_t i = 0; i < tempEnvMapIdx.size(); i += 4) {
		const ImageLUTEntry& diffuse = tempEnvMapIdx[i];
		const ImageLUTEntry& specular = tempEnvMapIdx[i + 1];
		const ImageLUTEntry& brdf = tempEnvMapIdx[i + 2];
		const ImageLUTEntry& skybox = tempEnvMapIdx[i + 3];

		ASSERT(diffuse.samplerCubeIndex != UINT32_MAX);
		ASSERT(specular.samplerCubeIndex != UINT32_MAX);
		ASSERT(brdf.combinedImageIndex != UINT32_MAX);
		ASSERT(skybox.samplerCubeIndex != UINT32_MAX);

		glm::vec4 envEntry{};
		envEntry.x = static_cast<float>(diffuse.samplerCubeIndex);
		envEntry.y = static_cast<float>(specular.samplerCubeIndex);
		envEntry.z = static_cast<float>(brdf.combinedImageIndex);
		envEntry.w = static_cast<float>(skybox.samplerCubeIndex);

		ASSERT(setIndex < MAX_ENV_SETS && "Too many environment sets for fixed UBO buffer!");
		ResourceManager::_envMapIndices.indices[setIndex++] = envEntry;
	}

	auto allocator = _resources.getAllocator();

	_resources.envMapSetUBO = BufferUtils::createBuffer(sizeof(GPUEnvMapIndices),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, allocator);

	GPUEnvMapIndices* envMapIndices = reinterpret_cast<GPUEnvMapIndices*>(_resources.envMapSetUBO.mapped);
	*envMapIndices = ResourceManager::_envMapIndices;

	auto set = DescriptorSetOverwatch::getUnifiedDescriptors().descriptorSet;
	DescriptorWriter mainWriter;
	mainWriter.writeBuffer(
		0,
		_resources.getAddressTableBuffer().buffer,
		_resources.getAddressTableBuffer().info.size,
		0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		set);
	mainWriter.writeBuffer(
		1,
		_resources.envMapSetUBO.buffer,
		sizeof(GPUEnvMapIndices),
		0,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		set);

	//for (int i = 0; i < MAX_ENV_SETS; i++) {
	//	const auto& e = ResourceManager::_envMapIndices.indices[i];
	//	fmt::print("[EnvMap {}] Diffuse: {:.0f}, Specular: {:.0f}, BRDF: {:.0f}, Skybox: {:.0f}\n",
	//		i, e.x, e.y, e.z, e.w);
	//}

	mainWriter.writeFromImageLUT(_resources.getImageLUT(), ResourceManager::_globalImageTable, set);
	mainWriter.updateSet(Backend::getDevice(), set);

	// VRAM Usage calculator
	auto physicalDevice = Backend::getPhysicalDevice();
	VkDeviceSize totalUsedVRAM = 0;

	totalUsedVRAM += profiler.GetTotalVRAMUsage(physicalDevice, mainAllocator);
	profiler.getStats().vramUsed = totalUsedVRAM;
	EngineStages::SetGoal(ENGINE_STAGE_READY);
}

void EngineState::renderFrame() {
	auto& frame = Renderer::getCurrentFrame();

	EditorImgui::renderImgui();

	Renderer::prepareFrameContext(frame);
	if (frame.swapchainResult != VK_SUCCESS) return;

	auto& profiler = Engine::getProfiler();
	// Stats reset and scene update start timer
	if (!profiler.getStats().forcedReset) {
		profiler.resetDrawCalls();
		profiler.resetTriangleCount();
	}
	else {
		profiler.getStats().forcedReset = false;
	}

	profiler.startTimer();
	RenderScene::updateScene(frame, _resources);
	auto elapsed = profiler.endTimer();
	profiler.getStats().sceneUpdateTime = elapsed;

	profiler.startTimer();
	Renderer::recordRenderCommand(frame);
	elapsed = profiler.endTimer();
	profiler.getStats().drawTime = elapsed;

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
		ASSERT(threadCtx.stagingBuffer.buffer == VK_NULL_HANDLE);
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