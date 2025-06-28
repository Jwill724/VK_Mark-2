#include "pch.h"

#include "common/Vk_Types.h"
#include "RenderScene.h"
#include "Renderer.h"
#include "vulkan/Backend.h"
#include "renderer/gpu_types/PipelineManager.h"
#include "renderer/gpu_types/Descriptor.h"
#include "renderer/gpu_types/CommandBuffer.h"
#include "SceneGraph.h"
#include "DrawPreperation.h"
#include "Visibility.h"
#include "core/Environment.h"
#include "utils/BufferUtils.h"
#include "core/ResourceManager.h"
#include "profiler/Profiler.h"

namespace RenderScene {
	GPUSceneData _sceneData;
	GPUSceneData& getCurrentSceneData() { return _sceneData; }

	std::vector<MeshID> _currentSceneMeshIDs;

	std::vector<glm::mat4> _transformsList;
	std::unordered_map<uint32_t, glm::mat4> _meshIDToTransforms;

	Camera _mainCamera;
	glm::mat4 _curCamView;
	glm::mat4 _curCamProj;

	// Only wanna extract a new frustum if viewproj changes
	glm::mat4 _lastViewProj;
	bool _isFirstViewProj = false;

	Frustum _currentFrustum;
}

void RenderScene::setScene() {
	_mainCamera._velocity = glm::vec3(0.0f);
	_mainCamera._position = SPAWNPOINT;

	_mainCamera._pitch = 0;
	_mainCamera._yaw = -90.0f;

	_sceneData.ambientColor = glm::vec4(0.03f, 0.03f, 0.03f, 1.0f);
	_sceneData.sunlightColor = glm::vec4(1.0f, 0.96f, 0.87f, 5.0f);
	_sceneData.sunlightDirection = glm::normalize(glm::vec4(1.0f, 1.0f, -0.787f, 0.0f));
}

void RenderScene::updateCamera() {
	auto extent = Renderer::getDrawExtent();
	float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);

	_mainCamera.processInput(Engine::getWindow(), Engine::getProfiler().getStats().deltaTime);

	_curCamView = _mainCamera.getViewMatrix();

	_curCamProj = glm::perspective(glm::radians(70.f), aspect, 0.1f, 500.f);

	// invert the Y direction on projection matrix so that we are more similar
	// to opengl and gltf axis
	_curCamProj[1][1] *= -1;

	_sceneData.view = _curCamView;
	_sceneData.proj = _curCamProj;
	_sceneData.viewproj = _curCamProj * _curCamView;
	_sceneData.cameraPosition = glm::vec4(_mainCamera._position, 0.0f);
}


// Draw preperation work
// Temporary, need a place to center ideas
void RenderScene::updateScene(FrameContext& frameCtx, GPUResources& resources) {
	// === Update and draw scene ===
	updateCamera();

	// first frustum extracted to start chain of reuse
	if (!_isFirstViewProj) {
		_lastViewProj = _sceneData.viewproj;
		_currentFrustum = Visibility::extractFrustum(_sceneData.viewproj);
		uploadFrustumToFrame(frameCtx.cullingPCData);
		_isFirstViewProj = true;
	}

	if (_sceneData.viewproj != _lastViewProj) {
		_lastViewProj = _sceneData.viewproj;
		_currentFrustum = Visibility::extractFrustum(_sceneData.viewproj);
		uploadFrustumToFrame(frameCtx.cullingPCData);
	}

	allocateSceneBuffer(frameCtx, resources.getAllocator());

	auto device = Backend::getDevice();

	// No scene loaded in
	if (_loadedScenes.empty()) {
		frameCtx.writer.clear();

		// Fully empty storage buffer writing
		// Only update ssbo once if no scene is loaded
		if (!frameCtx.addressTableDirty) {
			frameCtx.writer.writeBuffer(
				0,
				frameCtx.addressTableBuffer.buffer,
				frameCtx.addressTableBuffer.info.size,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				frameCtx.set
			);

			frameCtx.addressTableDirty = true;
		}

		// The one write needed for scene uniform
		frameCtx.writer.writeBuffer(
			1,
			frameCtx.sceneDataBuffer.buffer,
			sizeof(GPUSceneData),
			0,
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			frameCtx.set
		);

		frameCtx.writer.updateSet(device, frameCtx.set);

		return;
	}

	auto& tQueue = Backend::getTransferQueue();
	const auto allocator = resources.getAllocator();
	auto& meshes = resources.getResgisteredMeshes();


	if (frameCtx.transformsUpdated) {
		const uint32_t meshCount = static_cast<uint32_t>(meshes.meshData.size());
		frameCtx.cullingPCData.meshCount = meshCount;
		bakeTransformsFromSceneGraph(meshCount);
		frameCtx.transformsUpdated = false; // baked until new transforms applied
	}

	// Frame 0 will define all buffers needed until either new meshes or transforms
	// After data is defined it'll only need to update frustum each frame
	if (!frameCtx.meshDataSet && !frameCtx.transformsUpdated) {
		if (GPU_ACCELERATION_ENABLED) {
			// Prepare buffers where visibles will go, meshIDs
			DrawPreperation::meshDataAndTransformsListUpload(
				frameCtx,
				meshes,
				_transformsList,
				tQueue,
				allocator,
				GPU_ACCELERATION_ENABLED // false in current build, no transforms on the gpu
			);

			frameCtx.meshDataSet = true;

			frameCtx.writer.clear();
			frameCtx.writer.writeBuffer(
				0,
				frameCtx.addressTableBuffer.buffer,
				frameCtx.addressTableBuffer.info.size,
				0,
				VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
				frameCtx.set
			);
			frameCtx.writer.updateSet(device, frameCtx.set);
		}
		else {
			_currentSceneMeshIDs = meshes.extractAllMeshIDs();
			frameCtx.visibleMeshIDs.reserve(_currentSceneMeshIDs.size());
			for (const auto id : _currentSceneMeshIDs) {
				meshes.meshData[id].worldAABB = Visibility::transformAABB(meshes.meshData[id].localAABB, _transformsList[id]);
			}
			frameCtx.meshDataSet = true;
		}
	}

	// === CULLING PASS ===

	// gpu culling
	if (GPU_ACCELERATION_ENABLED) {
		VkDescriptorSet sets[] = {
			DescriptorSetOverwatch::getUnifiedDescriptors().descriptorSet,
			frameCtx.set
		};

		auto& computeSync = Renderer::_computeSync;

		auto& computeQueue = Backend::getComputeQueue();

		if (Backend::getTransferQueue().wasUsed) {
			computeQueue.waitTimelineValue(device, Renderer::_transferSync.semaphore, frameCtx.transferWaitValue);
		}

		CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {
			Visibility::performCulling(
				cmd,
				frameCtx.cullingPCData,
				frameCtx.stagingVisibleCountBuffer.buffer,
				frameCtx.stagingVisibleMeshIDsBuffer.buffer,
				frameCtx.gpuVisibleCountBuffer.buffer,
				frameCtx.gpuVisibleMeshIDsBuffer.buffer,
				sets);
		}, frameCtx.computePool, QueueType::Compute);

		frameCtx.computeCmds = DeferredCmdSubmitQueue::collectCompute();

		computeQueue.submitWithTimelineSync(
			frameCtx.computeCmds,
			computeSync.semaphore,
			computeSync.signalValue
		);

		frameCtx.computeWaitValue = computeSync.signalValue++;
		computeQueue.waitTimelineValue(device, computeSync.semaphore, frameCtx.computeWaitValue);

		memcpy(&frameCtx.visibleCount, frameCtx.stagingVisibleCountBuffer.mapped, sizeof(uint32_t));
		frameCtx.visibleMeshIDs.resize(frameCtx.visibleCount);
		memcpy(frameCtx.visibleMeshIDs.data(), frameCtx.stagingVisibleMeshIDsBuffer.mapped, sizeof(uint32_t) * frameCtx.visibleCount);
	}
	// CPU CULLING
	else {
		frameCtx.visibleCount = 0;
		for (const auto id : _currentSceneMeshIDs) {
			const MeshData& mesh = meshes.meshData[id];

			ASSERT(id < frameCtx.cullingPCData.meshCount && "id out of range in culling pass");

			bool visible = Visibility::isVisible(mesh.worldAABB, _currentFrustum);
			if (visible) {
				frameCtx.visibleMeshIDs.push_back(id);
				frameCtx.visibleCount++;
			}
		}
	}

	frameCtx.clearInstanceBuffers();

	if (frameCtx.visibleCount == 0) return;

	// Using the visible meshIds in culling, find all instances and define obj data
	updateVisiblesObjects(frameCtx);
	ASSERT(
		frameCtx.opaqueInstances.size() + frameCtx.transparentInstances.size() <= frameCtx.visibleCount &&
		"Instance list size exceeds visible mesh count!"
	);

	// Apply instance indices in draws with visiblecounts for each pass type
	if (!frameCtx.opaqueInstances.empty()) {
		frameCtx.opaqueIndirectDraws.resize(frameCtx.opaqueInstances.size());
		frameCtx.opaqueVisibleCount = static_cast<uint32_t>(frameCtx.opaqueInstances.size());
		for (int i = 0; i < frameCtx.opaqueInstances.size(); ++i) {
			frameCtx.opaqueIndirectDraws[i].instanceIndex = i;
		}
	}

	if (!frameCtx.transparentInstances.empty()) {
		frameCtx.transparentIndirectDraws.resize(frameCtx.transparentInstances.size());
		frameCtx.transparentVisibleCount = static_cast<uint32_t>(frameCtx.transparentInstances.size());
		for (int i = 0; i < frameCtx.transparentInstances.size(); ++i) {
			frameCtx.transparentIndirectDraws[i].instanceIndex = i;
		}
	}

	DrawPreperation::buildAndSortIndirectDraws(frameCtx, resources.getDrawRanges(), meshes.meshData);

	DrawPreperation::uploadGPUBuffersForFrame(frameCtx, tQueue, allocator);

	// Depending on if theres visibles, this could be the first and only write for the storage buffer
	// This ssbo write is for building the draws, the next will occur for actual drawing
	// If no visibles are present, early outs upload and table isn't marked dirty
	//
	// I think this address table dirty will only be useful for outside function, as of now it does nothing
	// within this packed single function setup
	bool descriptorWriteNeeded = false;
	if (frameCtx.addressTableDirty) {
		frameCtx.writer.clear();
		frameCtx.writer.writeBuffer(
			0,
			frameCtx.addressTableBuffer.buffer,
			frameCtx.addressTableBuffer.info.size,
			0,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			frameCtx.set
		);
		descriptorWriteNeeded = true;
		frameCtx.addressTableDirty = false;
	}

	if (!descriptorWriteNeeded)
		frameCtx.writer.clear(); // Only clear if it wasn't already cleared

	frameCtx.writer.writeBuffer(
		1,
		frameCtx.sceneDataBuffer.buffer,
		sizeof(GPUSceneData),
		0,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		frameCtx.set
	);
	frameCtx.writer.updateSet(device, frameCtx.set);
}

void RenderScene::bakeTransformsFromSceneGraph(const uint32_t meshCount) {
	// Update transforms
	for (auto& [name, scene] : _loadedScenes) {
		if (!scene) continue;

		for (auto& node : scene->scene.topNodes) {
			node->refreshTransform(glm::mat4(1.0f));
		}
	}

	_transformsList.clear();
	_transformsList.resize(meshCount, glm::mat4(0.0f));

	for (auto& [name, scene] : _loadedScenes) {
		if (!scene) continue;
		scene->FlattenSceneToTransformList(glm::mat4{ 1.0f }, _meshIDToTransforms);
	}

	for (uint32_t i = 0; i < meshCount; ++i) {
		auto it = _meshIDToTransforms.find(i);
		_transformsList[i] = (it != _meshIDToTransforms.end()) ? it->second : glm::mat4(0.0f);
	}
}

void RenderScene::updateVisiblesObjects(FrameContext& frameCtx) {
	// Convert visible mesh ID list to set
	const std::unordered_set<uint32_t> visibleMeshSet {
		frameCtx.visibleMeshIDs.begin(),
		frameCtx.visibleMeshIDs.end()
	};

	for (auto& [name, scene] : _loadedScenes) {
		if (!scene) continue;

		scene->FindVisibleObjects(
			frameCtx.opaqueInstances,
			frameCtx.transparentInstances,
			_meshIDToTransforms,
			visibleMeshSet);
	}
}

void RenderScene::allocateSceneBuffer(FrameContext& frameCtx, const VmaAllocator allocator) {
	frameCtx.sceneDataBuffer = BufferUtils::createBuffer(sizeof(GPUSceneData),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, allocator);

	ASSERT(frameCtx.sceneDataBuffer.buffer != VK_NULL_HANDLE);
	ASSERT(frameCtx.sceneDataBuffer.mapped != nullptr);

	auto sceneBuf = frameCtx.sceneDataBuffer;
	frameCtx.cpuDeletion.push_function([sceneBuf, allocator]() mutable {
		BufferUtils::destroyBuffer(sceneBuf, allocator);
	});

	GPUSceneData* sceneDataPtr = reinterpret_cast<GPUSceneData*>(frameCtx.sceneDataBuffer.mapped);
	*sceneDataPtr = _sceneData;
}

void RenderScene::renderGeometry(FrameContext& frameCtx) {
	auto extent = Renderer::getDrawExtent();

	// all pipelines share push constant and descriptor setup
	auto defaultPC = Pipelines::_globalLayout.pcRange;

	auto& profiler = Engine::getProfiler();

	// === SKYBOX DRAW ===
	{
		VulkanUtils::defineViewportAndScissor(frameCtx.commandBuffer, { extent.width, extent.height });
		vkCmdBindPipeline(frameCtx.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipelines::skyboxPipeline.pipeline);

		glm::mat4 view = glm::mat4(glm::mat3(_sceneData.view)); // strip translation

		glm::mat4 proj = _sceneData.proj;
		glm::mat4 viewproj = proj * view;

		glm::mat4 invVp = glm::inverse(viewproj);

		vkCmdPushConstants(frameCtx.commandBuffer,
			Pipelines::_globalLayout.layout,
			defaultPC.stageFlags,
			defaultPC.offset,
			defaultPC.size,
			&invVp);

		vkCmdDraw(frameCtx.commandBuffer, 3, 1, 0, 0);
		profiler.addDrawCall(1);
	}

	if (frameCtx.visibleCount == 0) {
		return;
	}

	auto& resources = Engine::getState().getGPUResources();

	drawBatches(frameCtx, resources);

	// === VISIBLE AABB FOR OBJECTS ===
	{
		if (profiler.debugToggles.showAABBs) {
			std::vector<glm::vec3> allVerts;
			std::vector<uint32_t> drawOffsets;

			auto& meshes = resources.getResgisteredMeshes().meshData;

			auto emitAABBVerts = [&](const GPUInstance& inst) {
				const auto& aabb = meshes[inst.meshID].worldAABB;
				auto verts = Visibility::GetAABBVertices(aabb);
				uint32_t offset = static_cast<uint32_t>(allVerts.size());
				drawOffsets.push_back(offset);
				allVerts.insert(allVerts.end(), verts.begin(), verts.end());
			};
			for (const auto& inst : frameCtx.opaqueInstances) emitAABBVerts(inst);
			for (const auto& inst : frameCtx.transparentInstances) emitAABBVerts(inst);

			auto allocator = resources.getAllocator();

			const size_t totalSize = sizeof(glm::vec3) * allVerts.size();

			AllocatedBuffer aabbVBO = BufferUtils::createBuffer(
				totalSize,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
				VMA_MEMORY_USAGE_CPU_TO_GPU,
				allocator);
			ASSERT(aabbVBO.info.pMappedData != nullptr);
			memcpy(aabbVBO.mapped, allVerts.data(), totalSize);

			frameCtx.cpuDeletion.push_function([aabbVBO, allocator]() mutable {
				BufferUtils::destroyBuffer(aabbVBO, allocator);
			});

			VulkanUtils::defineViewportAndScissor(frameCtx.commandBuffer, { extent.width, extent.height });
			vkCmdBindPipeline(frameCtx.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipelines::boundingBoxPipeline.pipeline);

			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(frameCtx.commandBuffer, 0, 1, &aabbVBO.buffer, &offset);

			struct alignas(16) AABBPushConstant {
				glm::mat4 worldMatrix;
				VkDeviceAddress vertexBuffer;
				uint32_t _pad[2];
			} pc{};
			pc.worldMatrix = _sceneData.viewproj;
			pc.vertexBuffer = aabbVBO.address;

			vkCmdPushConstants(frameCtx.commandBuffer,
				Pipelines::_globalLayout.layout,
				defaultPC.stageFlags,
				defaultPC.offset,
				defaultPC.size,
				&pc
			);

			const uint32_t vertsPerAABB = 24;
			for (uint32_t i = 0; i < drawOffsets.size(); ++i) {
				uint32_t vertexOffset = drawOffsets[i];
				vkCmdDraw(frameCtx.commandBuffer, vertsPerAABB, 1, vertexOffset, 0);
				profiler.addDrawCall(1);
			}
		}
	}
}

void RenderScene::drawBatches(const FrameContext& frameCtx, GPUResources& resources) {
	auto pLayout = Pipelines::_globalLayout;
	auto extent = Renderer::getDrawExtent();

	auto& profiler = Engine::getProfiler();

	auto& idxBuffer = resources.getBuffer(AddressBufferType::Index);
	const auto& ranges = resources.getDrawRanges();
	const auto& meshes = resources.getResgisteredMeshes().meshData;

	// All pipelines use the same layout
	VkPipeline pipeline{};
	if (profiler.pipeOverride.enabled)
		pipeline = profiler.getPipelineByType(profiler.pipeOverride.selected);
	else
		pipeline = Pipelines::opaquePipeline.pipeline; // default

	VulkanUtils::defineViewportAndScissor(frameCtx.commandBuffer, { extent.width, extent.height });

	constexpr VkDeviceSize drawCmdSize = sizeof(VkDrawIndexedIndirectCommand);

	struct alignas(16) DrawPushConstants {
		uint32_t opaqueVisibleCount;
		uint32_t transparentVisibleCount;
		uint32_t pad[2];
	} pc{};
	pc.opaqueVisibleCount = frameCtx.opaqueVisibleCount;
	pc.transparentVisibleCount = frameCtx.transparentVisibleCount;

	vkCmdBindPipeline(frameCtx.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	vkCmdBindIndexBuffer(frameCtx.commandBuffer, idxBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
	vkCmdPushConstants(frameCtx.commandBuffer,
		pLayout.layout,
		pLayout.pcRange.stageFlags,
		pLayout.pcRange.offset,
		pLayout.pcRange.size,
		&pc);

	if (frameCtx.opaqueVisibleCount > 0) {
		// === Batched opaque draw ===
		for (const auto& batch : frameCtx.opaqueIndirectDraws) {
			VkDeviceSize offset = batch.drawOffset * drawCmdSize;

			vkCmdDrawIndexedIndirect(
				frameCtx.commandBuffer,
				frameCtx.opaqueIndirectCmdBuffer.buffer,
				offset,
				1,
				drawCmdSize
			);

			uint32_t triangleCount = (batch.cmd.indexCount * batch.cmd.instanceCount) / 3;
			profiler.addDrawCall(triangleCount);
		}
	}

	if (frameCtx.transparentVisibleCount > 0) {
		if (!profiler.pipeOverride.enabled) {
			vkCmdBindPipeline(frameCtx.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipelines::transparentPipeline.pipeline);
		}

		for (uint32_t i = 0; i < frameCtx.transparentVisibleCount; ++i) {
			VkDeviceSize offset = i * drawCmdSize;

			vkCmdDrawIndexedIndirect(
				frameCtx.commandBuffer,
				frameCtx.transparentIndirectCmdBuffer.buffer,
				offset,
				1,
				drawCmdSize
			);

			auto& meshID = meshes[frameCtx.transparentInstances[i].meshID];
			uint32_t triangleCount = ranges[meshID.drawRangeIndex].indexCount / 3;
			profiler.addDrawCall(triangleCount);
		}
	}
}

void RenderScene::uploadFrustumToFrame(CullingPushConstantsAddrs& frustumData) {
	if (!GPU_ACCELERATION_ENABLED) return;

	std::copy(
		std::begin(_currentFrustum.planes),
		std::end(_currentFrustum.planes),
		std::begin(frustumData.frusPlanes)
	);

	std::copy(
		std::begin(_currentFrustum.points),
		std::end(_currentFrustum.points),
		std::begin(frustumData.frusPoints)
	);
}