#include "pch.h"

#include "EngineState.h"
#include "vulkan/Backend.h"
#include "common/Vk_Types.h"
#include "common/EngineConstants.h"
#include "renderer/gpu/CommandBuffer.h"
#include "renderer/gpu/Descriptor.h"
#include "renderer/renderer.h"
#include "utils/VulkanUtils.h"
#include "AssetManager.h"
#include "Environment.h"
#include "JobSystem.h"
#include "renderer/RenderScene.h"
#include "renderer/SceneGraph.h"
#include "renderer/Batching.h"
#include "profiler/EditorImgui.h"
#include "Engine.h"

std::vector<ThreadContext>& allThreadContexts = getAllThreadContexts();

void EngineState::init() {
	auto device = Backend::getDevice();

	JobSystem::initScheduler();

	uint32_t graphicsIndex = Backend::getGraphicsQueue().familyIndex;
	uint32_t transferIndex = Backend::getTransferQueue().familyIndex;
	JobSystem::getThreadPoolManager().init(device, static_cast<uint32_t>(allThreadContexts.size()), graphicsIndex, transferIndex);

	_resources.init();
	auto& dQueue = _resources.getMainDeletionQueue();
	auto mainAllocator = _resources.getAllocator();

	EditorImgui::initImgui(dQueue);
	DescriptorSetOverwatch::initDescriptors(dQueue);

	auto frameLayout = DescriptorSetOverwatch::getFrameDescriptors().descriptorLayout;
	Renderer::initFrameContexts(device, graphicsIndex, transferIndex, Engine::getWindowExtent(), frameLayout, mainAllocator);
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
	_resources.getImageLUT().clear();
	ResourceManager::_globalImageTable.clearTables();
}

void EngineState::loadAssets() {
	auto assetQueue = std::make_shared<GLTFAssetQueue>();

	auto& profiler = Engine::getProfiler();
	profiler.startTimer();

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

	if (availableAssets) {
		fmt::print("Assets available for loading!\n");

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
		// clear material staging buffers
		_resources.getTempDeletionQueue().flush();

		JobSystem::submitJob([assetQueue](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			AssetManager::processMeshes(threadCtx);
			EngineStages::SetGoal(ENGINE_STAGE_LOADING_MESHES_READY);
		});

		JobSystem::wait();

		// Mesh buffer prep
		JobSystem::submitJob([assetQueue](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
			assert(queue);

			auto gltfJobs = queue->collect();

			auto& resources = Engine::getState().getGPUResources();
			auto& drawRanges = resources.getDrawRanges();

			for (auto& context : gltfJobs) {
				if (!context->isJobComplete(GLTFJobType::ProcessMeshes)) continue;

				size_t vertexOffset = 0;
				size_t indexOffset = 0;

				auto& uploadCtx = context->uploadMeshCtx;
				uploadCtx.meshViews.reserve(uploadCtx.meshHandles.size());

				for (auto& mesh : uploadCtx.meshHandles) {
					for (auto& matHandle : mesh->materialHandles) {
						assert(matHandle->instance->drawRangeIndex < drawRanges.size());
						const auto rangeIndex = matHandle->instance->drawRangeIndex;
						const GPUDrawRange& range = drawRanges[rangeIndex];
						const size_t rangeIndexCount = range.indexCount;
						const size_t rangeVertexCount = range.vertexCount;

						fmt::print("[Prep] RangeIdx={} IndexCount={} VertexCount={} VertexOffset={} IndexOffset={}\n",
							rangeIndex, rangeIndexCount, rangeVertexCount, vertexOffset, indexOffset);

						assert(drawRanges[rangeIndex].firstIndex == indexOffset);
						assert(drawRanges[rangeIndex].vertexOffset == vertexOffset);

						std::span<Vertex> vertexSpan = {
							uploadCtx.globalVertices.data() + vertexOffset, rangeVertexCount
						};
						std::span<uint32_t> indexSpan = {
							uploadCtx.globalIndices.data() + indexOffset, rangeIndexCount
						};

						UploadedMeshView newMeshView;
						newMeshView.drawRangeIndex = rangeIndex;
						newMeshView.vertexData = vertexSpan;
						newMeshView.indexData = indexSpan;

						uploadCtx.meshViews.push_back(newMeshView);

						vertexOffset += rangeVertexCount;
						indexOffset += rangeIndexCount;
					}
				}

				queue->push(context);
				context->markJobComplete(GLTFJobType::MeshBufferReady);
			}
			EngineStages::SetGoal(ENGINE_STAGE_LOADING_UPLOAD_MESH_DATA);
		});

		JobSystem::wait();

		// Mesh upload
		JobSystem::submitJob([assetQueue, mainAllocator](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			threadCtx.cmdPool = JobSystem::getThreadPoolManager().getPool(threadCtx.threadID, QueueType::Transfer);
			auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
			assert(queue);

			auto gltfJobs = queue->collect();

			size_t totalVertexCount = 0;
			size_t totalIndexCount = 0;

			auto& resources = Engine::getState().getGPUResources();

			for (auto& context : gltfJobs) {
				for (auto& meshView : context->uploadMeshCtx.meshViews) {
					totalVertexCount += meshView.vertexData.size();
					totalIndexCount += meshView.indexData.size();
				}
			}

			const size_t vertexBufferSize = totalVertexCount * sizeof(Vertex);
			const size_t indexBufferSize = totalIndexCount * sizeof(uint32_t);

			auto& vtxBuffer = resources.getVertexBuffer();
			auto& idxBuffer = resources.getIndexBuffer();

			// Create the large GPU buffers
			vtxBuffer = BufferUtils::createBuffer(
				vertexBufferSize,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
				VK_BUFFER_USAGE_TRANSFER_DST_BIT |
				VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
				VMA_MEMORY_USAGE_GPU_ONLY,
				mainAllocator
			);
			idxBuffer = BufferUtils::createBuffer(
				indexBufferSize,
				VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
				VK_BUFFER_USAGE_TRANSFER_DST_BIT |
				VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
				VMA_MEMORY_USAGE_GPU_ONLY,
				mainAllocator
			);

			threadCtx.stagingBuffer = BufferUtils::createBuffer(
				vertexBufferSize + indexBufferSize,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VMA_MEMORY_USAGE_CPU_ONLY,
				mainAllocator
			);
			assert(threadCtx.stagingBuffer.info.size >= vertexBufferSize + indexBufferSize);

			auto buffer = threadCtx.stagingBuffer;
			resources.getTempDeletionQueue().push_function([buffer, mainAllocator]() mutable {
				BufferUtils::destroyBuffer(buffer, mainAllocator);
				});
			threadCtx.stagingMapped = threadCtx.stagingBuffer.info.pMappedData;
			assert(threadCtx.stagingMapped != nullptr);

			// Fill staging buffer
			uint8_t* stagingData = reinterpret_cast<uint8_t*>(threadCtx.stagingMapped);

			auto& drawRanges = resources.getDrawRanges();

			VkDeviceSize vertexWriteOffset = 0;
			VkDeviceSize indexWriteOffset = vertexBufferSize;

			CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {
				for (auto& context : gltfJobs) {
					if (!context->isComplete()) continue;

					auto& uploadCtx = context->uploadMeshCtx;

					for (const auto& meshView : uploadCtx.meshViews) {
						assert(meshView.drawRangeIndex < drawRanges.size());

						const GPUDrawRange& range = drawRanges[meshView.drawRangeIndex];
						assert(range.vertexCount == meshView.vertexData.size());
						assert(range.indexCount == meshView.indexData.size());

						const VkDeviceSize vertexSize = range.vertexCount * sizeof(Vertex);
						const VkDeviceSize indexSize = range.indexCount * sizeof(uint32_t);

						// Copy vertex data into staging buffer
						std::memcpy(stagingData + vertexWriteOffset, meshView.vertexData.data(), vertexSize);
						VkBufferCopy vtxCopy{
							.srcOffset = vertexWriteOffset,
							.dstOffset = range.vertexOffset * sizeof(Vertex),
							.size = vertexSize
						};
						vkCmdCopyBuffer(cmd, threadCtx.stagingBuffer.buffer, vtxBuffer.buffer, 1, &vtxCopy);
						vertexWriteOffset += vertexSize;

						// Copy index data into staging buffer
						std::memcpy(stagingData + indexWriteOffset, meshView.indexData.data(), indexSize);
						VkBufferCopy idxCopy{
							.srcOffset = indexWriteOffset,
							.dstOffset = range.firstIndex * sizeof(uint32_t),
							.size = indexSize
						};
						vkCmdCopyBuffer(cmd, threadCtx.stagingBuffer.buffer, idxBuffer.buffer, 1, &idxCopy);
						indexWriteOffset += indexSize;

						fmt::print("[Upload] DrawRangeIdx={} -> IndexCount={} VertexOffset={} FirstIndex={}\n",
							meshView.drawRangeIndex, range.indexCount, range.vertexOffset, range.firstIndex);
					}

					for (auto& mesh : uploadCtx.meshHandles) {
						for (auto& mat : mesh->materialHandles) {
							const auto& range = drawRanges[mat->instance->drawRangeIndex];

							fmt::print("[Verify] Instance={} IndexCount={} FirstIndex={} VertexOffset={}\n",
								mat->instance->drawRangeIndex, range.indexCount, range.firstIndex, range.vertexOffset);

							assert(range.vertexOffset * sizeof(Vertex) < vertexBufferSize);
							assert((range.firstIndex + range.indexCount) * sizeof(uint32_t) <= indexBufferSize);
						}
					}

					queue->push(context);
				}
			}, threadCtx.cmdPool, true);

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

		JobSystem::submitJob([assetQueue](ThreadContext& threadCtx) {
			ScopedWorkQueue scoped(threadCtx, assetQueue.get());
			SceneGraph::buildSceneGraph(threadCtx);

			auto* queue = dynamic_cast<GLTFAssetQueue*>(threadCtx.workQueueActive);
			assert(queue);

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
		fmt::print("Asset loading completed in {}.\n", elapsed);

		auto device = Backend::getDevice();

		// Draw range gpu buffer creation
		auto& drawRanges = _resources.getDrawRanges();
		if (!drawRanges.empty()) {
			AllocatedBuffer drawRangeBuffer = BufferUtils::createGPUAddressBuffer(
				AddressBufferType::DrawRange,
				_resources.getAddressTable(),
				drawRanges.size() * sizeof(GPUDrawRange),
				mainAllocator);
			_resources.addGPUBuffer(AddressBufferType::DrawRange, drawRangeBuffer);

			AllocatedBuffer drawRangeStaging = BufferUtils::createBuffer(
				drawRangeBuffer.info.size,
				VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				VMA_MEMORY_USAGE_CPU_ONLY,
				mainAllocator);
			memcpy(drawRangeStaging.info.pMappedData, drawRanges.data(), drawRangeBuffer.info.size);

			auto& transferPool = _resources.getTransferPool();
			CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {
				VkBufferCopy copyRegion{};
				copyRegion.size = drawRangeBuffer.info.size;
				vkCmdCopyBuffer(cmd, drawRangeStaging.buffer, drawRangeBuffer.buffer, 1, &copyRegion);
			}, transferPool, true);

			_resources.updateAddressTableMapped(transferPool);

			auto& tQueue = Backend::getTransferQueue();
			_resources.getLastSubmittedFence() = Engine::getState().submitCommandBuffers(tQueue);
			waitAndRecycleLastFence(_resources.getLastSubmittedFence(), tQueue, device);

			VK_CHECK(vkResetCommandPool(device, transferPool, 0));
			BufferUtils::destroyBuffer(drawRangeStaging, mainAllocator);
		}
	}
	else {
		fmt::print("No assets for loading... skipping\n");
	}

	// Perma imagelut work starts here
	auto& postProcessImg = ResourceManager::getPostProcessImage();
	auto& drawImg = ResourceManager::getDrawImage();
	postProcessImg.lutEntry.storageImageIndex = ResourceManager::_globalImageTable.pushStorage(postProcessImg.imageView);
	postProcessImg.lutEntry.combinedImageIndex = ResourceManager::_globalImageTable.pushCombined(drawImg.imageView,
		ResourceManager::getDefaultSamplerLinear());
	_resources.addImageLUTEntry(postProcessImg.lutEntry);

	// === ENVIRONMENT IMAGE SETUP ===
	auto& skyboxImg = ResourceManager::getSkyBoxImage();
	auto& skyboxSmpl = ResourceManager::getSkyBoxSampler();

	auto& diffuseImg = ResourceManager::getIrradianceImage();
	auto& diffuseSmpl = ResourceManager::getIrradianceSampler();

	auto& specImg = ResourceManager::getSpecularPrefilterImage();
	auto& specSmpl = ResourceManager::getSpecularPrefilterSampler();

	auto& brdfImg = ResourceManager::getBRDFImage();
	auto& brdfSmpl = ResourceManager::getBRDFSampler();

	auto& sceneData = RenderScene::getCurrentSceneData();

	diffuseImg.lutEntry.samplerCubeIndex = ResourceManager::_globalImageTable.pushSamplerCube(diffuseImg.imageView, diffuseSmpl);
	sceneData.envMapIndex.x = static_cast<float>(diffuseImg.lutEntry.samplerCubeIndex);
	_resources.addImageLUTEntry(diffuseImg.lutEntry);

	specImg.lutEntry.samplerCubeIndex = ResourceManager::_globalImageTable.pushSamplerCube(specImg.imageView, specSmpl);
	sceneData.envMapIndex.y = static_cast<float>(specImg.lutEntry.samplerCubeIndex);
	_resources.addImageLUTEntry(specImg.lutEntry);

	brdfImg.lutEntry.combinedImageIndex = ResourceManager::_globalImageTable.pushCombined(brdfImg.imageView, brdfSmpl);
	sceneData.envMapIndex.z = static_cast<float>(brdfImg.lutEntry.combinedImageIndex);
	_resources.addImageLUTEntry(brdfImg.lutEntry);

	skyboxImg.lutEntry.samplerCubeIndex = ResourceManager::_globalImageTable.pushSamplerCube(skyboxImg.imageView, skyboxSmpl);
	sceneData.envMapIndex.w = static_cast<float>(skyboxImg.lutEntry.samplerCubeIndex);
	_resources.addImageLUTEntry(skyboxImg.lutEntry);

	auto set = DescriptorSetOverwatch::getUnifiedDescriptors().descriptorSet;
	DescriptorWriter mainWriter;
	mainWriter.writeBuffer(0, _resources.getAddressTableBuffer().buffer,
		sizeof(GPUAddressTable), 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, set);
	mainWriter.writeFromImageLUT(_resources.getImageLUT(), ResourceManager::_globalImageTable, set);
	mainWriter.updateSet(Backend::getDevice(), set);

	// Post process color correction data setup
	auto idx0 = postProcessImg.lutEntry.combinedImageIndex;
	auto idx1 = postProcessImg.lutEntry.storageImageIndex;
	ColorData colorSettings {
		.brightness = 1.0f,
		.saturation = 1.0f,
		.contrast = 1.0f,
		.cmbViewIdx = idx0,
		.storageViewIdx = idx1
	};
	Pipelines::postProcessPipeline.getComputeEffect().setPushData(colorSettings);

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

	// Stats reset and scene update start timer
	auto& profiler = Engine::getProfiler();
	profiler.resetDrawCalls();
	profiler.resetTriangleCount();
	profiler.startTimer();
	Renderer::prepareFrameContext(frame);

	RenderScene::updateScene(frame, _resources);
	auto elapsed = profiler.endTimer();
	profiler.getStats().sceneUpdateTime = elapsed;
	profiler.startTimer();

	frame.inUse = true;
	Renderer::recordRenderCommand(frame);
	frame.inUse = false;
	elapsed = profiler.endTimer();
	profiler.getStats().drawTime = elapsed;

	Renderer::submitFrame(frame, _resources);
}

void EngineState::shutdown() {
	auto device = Backend::getDevice();

	JobSystem::shutdownScheduler();

	Backend::getGraphicsQueue().fencePool.destroy();
	Backend::getPresentQueue().fencePool.destroy();

	RenderScene::_loadedScenes.clear();

	JobSystem::getThreadPoolManager().cleanup(device);

	for (int i = 0; i < static_cast<int>(allThreadContexts.size()); ++i) {
		ThreadContext& threadCtx = allThreadContexts[i];
		threadCtx.deletionQueue.flush();
		assert(threadCtx.cmdPool == VK_NULL_HANDLE);
		assert(threadCtx.stagingBuffer.buffer == VK_NULL_HANDLE);
		assert(threadCtx.lastSubmittedFence == VK_NULL_HANDLE);
		assert(threadCtx.stagingMapped == nullptr);
	}

	_resources.getTempDeletionQueue().flush();
	_resources.getMainDeletionQueue().flush();

	Renderer::cleanup();

	_resources.cleanup(device);
}


// Takes graphics or transfer queues
// fence protected
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

	default:
		fmt::print("Unknown queue type.\n");
		return VK_NULL_HANDLE;
	}

	if (!cmds.empty()) {
		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = static_cast<uint32_t>(cmds.size());
		submitInfo.pCommandBuffers = cmds.data();

		VkFence lastSubmittedFence = queue.submit(submitInfo);
		return lastSubmittedFence;
	}
}