#include "pch.h"

#include "common/Vk_Types.h"
#include "RenderScene.h"
#include "Renderer.h"
#include "vulkan/Backend.h"
#include "renderer/gpu/PipelineManager.h"
#include "renderer/gpu/Descriptor.h"
#include "SceneGraph.h"
#include "Batching.h"
#include "Visibility.h"
#include "core/Environment.h"
#include "utils/BufferUtils.h"
#include "core/ResourceManager.h"
#include "profiler/Profiler.h"

namespace RenderScene {
	GPUSceneData _sceneData;
	GPUSceneData& getCurrentSceneData() { return _sceneData; }

	Camera _mainCamera;
	glm::mat4 _curCamView;
	glm::mat4 _curCamProj;

	Frustum _currentFrustum;

	std::vector<SortedBatch> _sortedOpaqueBatches;
	std::vector<RenderObject> _sortedTransparentList;

	DrawContext _mainDrawContext;
}

void RenderScene::setScene() {
	_mainCamera._velocity = glm::vec3(0.f);
	_mainCamera._position = SPAWNPOINT;

	_mainCamera._pitch = 0;
	_mainCamera._yaw = -90.f;

	_sceneData.ambientColor = glm::vec4(0.03f, 0.03f, 0.03f, 1.0f);
	_sceneData.sunlightColor = glm::vec4(1.0f, 0.96f, 0.87f, 5.0f);
	_sceneData.sunlightDirection = glm::normalize(glm::vec4(1.0f, 1.0f, -0.787f, 0.0f));
}

void RenderScene::updateCamera() {
	auto extent = Renderer::getDrawExtent();
	float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);

	_mainCamera.update(Engine::getWindow(), Engine::getProfiler().getStats().deltaTime);

	_curCamView = _mainCamera.getViewMatrix();
	_curCamProj = glm::perspective(glm::radians(70.f), aspect, 0.1f, 500.f);

	// invert the Y direction on projection matrix so that we are more similar
	// to opengl and gltf axis
	_curCamProj[1][1] *= -1;
}

void RenderScene::updateFrustum() {
	_sceneData.view = _curCamView;
	_sceneData.proj = _curCamProj;
	_sceneData.viewproj = _curCamProj * _curCamView;

	_currentFrustum = Visibility::extractFrustum(_sceneData.viewproj);
	_mainDrawContext.frustum = _currentFrustum;

	_sceneData.cameraPosition = glm::vec4(_mainCamera._position, 0.f);
}

void RenderScene::updateScene(FrameContext& frameCtx, GPUResources& resources) {
	frameCtx.opaqueRenderables.clear();
	frameCtx.transparentRenderables.clear();

	// === Update and draw scene ===
	updateCamera();
	updateFrustum();

	if (_loadedScenes.empty()) {
		return;
	}

	updateTransforms();
	updateSceneGraph();

	// === Assign unique instance indices from visibles before batching ===
	uint32_t currentInstanceIndex = 0;
	for (auto& obj : _mainDrawContext.OpaqueSurfaces)
		obj.instanceIndex = currentInstanceIndex++;
	for (auto& obj : _mainDrawContext.TransparentSurfaces)
		obj.instanceIndex = currentInstanceIndex++;

	frameCtx.opaqueRenderables = _mainDrawContext.OpaqueSurfaces;
	frameCtx.transparentRenderables = _mainDrawContext.TransparentSurfaces;

	_sortedOpaqueBatches.clear();
	_sortedTransparentList.clear();
	// === Perform batching for draw call grouping ===
	Batching::buildAndSortBatches(
		frameCtx.opaqueRenderables,
		frameCtx.transparentRenderables,
		resources.getDrawRanges(),
		_sortedOpaqueBatches,
		_sortedTransparentList
	);

	// === Merge opaque + transparent into one final visible list ===
	std::vector<RenderObject> allVisible;
	allVisible.reserve(frameCtx.opaqueRenderables.size() + frameCtx.transparentRenderables.size());
	allVisible.insert(allVisible.end(), frameCtx.opaqueRenderables.begin(), frameCtx.opaqueRenderables.end());
	allVisible.insert(allVisible.end(), frameCtx.transparentRenderables.begin(), frameCtx.transparentRenderables.end());

	// === Build buffers and upload to GPU ===
	Batching::buildInstanceBuffer(allVisible, frameCtx, resources);
	Batching::createIndirectCommandBuffer(allVisible, frameCtx, resources);
	Batching::uploadBuffersForFrame(frameCtx, resources, Backend::getTransferQueue());
}

// TODO: Need a better scene managment system where I don't need to comment shit out
void RenderScene::updateTransforms() {
	//for (auto& node : _loadedScenes["sponza"]->topNodes) {
	//	node->refreshTransform(glm::mat4(1.f));
	//}

	//for (auto& node : _loadedScenes["mrspheres"]->topNodes) {
	//	node->refreshTransform(glm::mat4(1.f));
	//}

	//for (auto& node : _loadedScenes["cube"]->topNodes) {
	//	node->refreshTransform(glm::mat4(1.f));
	//}

	//for (auto& node : _loadedScenes["damagedhelmet"]->topNodes) {
	//	node->refreshTransform(glm::mat4(1.f));
	//}
}

void RenderScene::updateSceneGraph() {
	_mainDrawContext.OpaqueSurfaces.clear();
	_mainDrawContext.TransparentSurfaces.clear();
	//_loadedScenes["sponza"]->Draw(glm::mat4{ 1.f }, _mainDrawContext);
	//_loadedScenes["mrspheres"]->Draw(glm::mat4{ 1.f }, _mainDrawContext);
	//_loadedScenes["cube"]->Draw(glm::mat4{ 1.f }, _mainDrawContext);
	//_loadedScenes["damagedhelmet"]->Draw(glm::mat4{ 1.f }, _mainDrawContext);
}

void RenderScene::allocateSceneBuffer(FrameContext& frameCtx, const VmaAllocator allocator) {
	frameCtx.sceneDataBuffer = BufferUtils::createBuffer(sizeof(GPUSceneData),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, allocator);

	auto sceneBuf = frameCtx.sceneDataBuffer;
	frameCtx.deletionQueue.push_function([sceneBuf, allocator]() mutable {
		BufferUtils::destroyBuffer(sceneBuf, allocator);
	});

	GPUSceneData* sceneDataPtr = reinterpret_cast<GPUSceneData*>(frameCtx.sceneDataBuffer.mapped);
	*sceneDataPtr = _sceneData;
}

void RenderScene::renderGeometry(FrameContext& frameCtx) {
	auto extent = Renderer::getDrawExtent();
	auto device = Backend::getDevice();

	auto& resources = Engine::getState().getGPUResources();

	// all pipelines share push constant and descriptor setup
	auto defaultPC = Pipelines::_globalLayout.pcRange;

	assert(frameCtx.addressTableBuffer.buffer != VK_NULL_HANDLE);
	assert(frameCtx.addressTableBuffer.info.size % 16 == 0);

	allocateSceneBuffer(frameCtx, resources.getAllocator());
	assert(frameCtx.sceneDataBuffer.buffer != VK_NULL_HANDLE);
	assert(frameCtx.sceneDataBuffer.mapped != nullptr);

	frameCtx.writer.clear();
	frameCtx.writer.writeBuffer(
		0,
		frameCtx.addressTableBuffer.buffer,
		frameCtx.addressTableBuffer.info.size,
		0,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		frameCtx.set
	);
	frameCtx.writer.writeBuffer(
		1,
		frameCtx.sceneDataBuffer.buffer,
		sizeof(GPUSceneData),
		0,
		VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
		frameCtx.set
	);
	frameCtx.writer.updateSet(device, frameCtx.set);

	auto& profiler = Engine::getProfiler();

	// === SKYBOX DRAW ===
	{
		vkCmdBindPipeline(frameCtx.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipelines::skyboxPipeline.pipeline);

		VulkanUtils::defineViewportAndScissor(frameCtx.commandBuffer, {extent.width, extent.height});

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

	if (_loadedScenes.empty()) {
		return;
	}

	drawBatches(frameCtx, _sortedOpaqueBatches, _sortedTransparentList, resources);

	// === VISIBLE AABB FOR OBJECTS ===
	{
		if (Engine::getProfiler().debugToggles.showAABBs) {
			std::vector<glm::vec3> allVerts;
			std::vector<uint32_t> drawOffsets;

			for (const auto& instance : frameCtx.instanceData) {
				AABB worldAABB = Visibility::transformAABB(instance.localAABB, instance.modelMatrix);
				auto verts = Visibility::GetAABBVertices(worldAABB);

				uint32_t offset = static_cast<uint32_t>(allVerts.size());
				drawOffsets.push_back(offset);
				allVerts.insert(allVerts.end(), verts.begin(), verts.end());
			}

			if (allVerts.empty()) {
				return; // Don't try to draw anything, prevent crash
			}

			auto allocator = resources.getAllocator();
			AllocatedBuffer aabbVBO = BufferUtils::createBuffer(
				sizeof(glm::vec3) * allVerts.size(),
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
				VMA_MEMORY_USAGE_CPU_TO_GPU,
				allocator);
			assert(aabbVBO.info.pMappedData != nullptr);
			memcpy(aabbVBO.mapped, allVerts.data(), sizeof(glm::vec3) * allVerts.size());

			frameCtx.deletionQueue.push_function([aabbVBO, allocator]() mutable {
				BufferUtils::destroyBuffer(aabbVBO, allocator);
			});

			vkCmdBindPipeline(frameCtx.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Pipelines::boundingBoxPipeline.pipeline);

			VulkanUtils::defineViewportAndScissor(frameCtx.commandBuffer, { extent.width, extent.height });

			VkDeviceSize offset = 0;
			vkCmdBindVertexBuffers(frameCtx.commandBuffer, 0, 1, &aabbVBO.buffer, &offset);

			struct alignas(16) AABBPushConstant {
				glm::mat4 worldMatrix;
				VkDeviceAddress vertexBuffer;
				uint32_t _pad[2];
			} pc{};
			pc.worldMatrix = _sceneData.viewproj;

			// must get the buffer address
			VkBufferDeviceAddressInfo info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
			info.buffer = aabbVBO.buffer;
			pc.vertexBuffer = vkGetBufferDeviceAddress(device, &info);

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

void RenderScene::drawBatches(
	const FrameContext& frameCtx,
	const std::vector<SortedBatch>& opaqueBatches,
	const std::vector<RenderObject>& transparentObjects,
	GPUResources& resources
) {
	auto defaultPCData = Pipelines::_globalLayout.pcRange;
	auto extent = Renderer::getDrawExtent();

	auto& profiler = Engine::getProfiler();

	auto& vtxBuffer = resources.getVertexBuffer();
	auto& idxBuffer = resources.getIndexBuffer();

	VulkanUtils::defineViewportAndScissor(frameCtx.commandBuffer, { extent.width, extent.height });

	fmt::print("[Renderer] Drawing {} opaque batches...\n", opaqueBatches.size());

	// === Batched opaque draw ===
	for (const auto& batch : opaqueBatches) {
		if (profiler.pipeOverride.enabled) {
			auto pipeline = profiler.getPipelineByType(profiler.pipeOverride.selected);
			batch.pipeline->pipeline = pipeline->pipeline;
		}

		fmt::print("  [Batch] Drawing {} draw commands (drawOffset: {})\n", batch.cmds.size(), batch.drawOffset);

		for (size_t i = 0; i < batch.cmds.size(); ++i) {
			const auto& cmd = batch.cmds[i];
			const size_t finalIndex = static_cast<size_t>(cmd.cmd.firstIndex + cmd.cmd.indexCount) * sizeof(uint32_t);
			assert(finalIndex <= idxBuffer.info.size);
		}

		vkCmdBindPipeline(frameCtx.commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			batch.pipeline->pipeline);
		vkCmdBindIndexBuffer(frameCtx.commandBuffer,
			idxBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

		vkCmdDrawIndexedIndirect(frameCtx.commandBuffer,
			frameCtx.indirectCmdBuffer.buffer,
			batch.drawOffset * sizeof(VkDrawIndexedIndirectCommand),
			static_cast<uint32_t>(batch.cmds.size()),
			sizeof(VkDrawIndexedIndirectCommand)
		);

		for (const auto& cmd : batch.cmds) {
			profiler.addDrawCall(cmd.cmd.indexCount / 3);
		}
	}

	if (!transparentObjects.empty()) {
		fmt::print("[Renderer] Drawing {} transparent objects individually...\n", transparentObjects.size());

		// === Individually sorted transparent draw ===
		vkCmdBindPipeline(frameCtx.commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			Pipelines::transparentPipeline.pipeline);
		vkCmdBindIndexBuffer(frameCtx.commandBuffer,
			idxBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

		for (const auto& obj : transparentObjects) {
			DrawPushConstants pc{};
			pc.materialIndex = obj.materialIndex;
			pc.modelMatrix = obj.modelMatrix;
			pc.drawRangeIndex = obj.drawRangeIndex;
			pc.vertexAddress = vtxBuffer.address;
			pc.indexAddress = idxBuffer.address;

			const auto& drawRange = resources.getDrawRanges()[obj.drawRangeIndex];

			fmt::print("  [Transparent {}] IndexCount={}, FirstIndex={}, VertexOffset={}, InstanceIndex={}\n",
				obj.instanceIndex, drawRange.indexCount, drawRange.firstIndex, drawRange.vertexOffset, obj.instanceIndex);

			vkCmdPushConstants(frameCtx.commandBuffer,
				Pipelines::_globalLayout.layout,
				defaultPCData.stageFlags,
				defaultPCData.offset,
				defaultPCData.size,
				&pc);

			vkCmdDrawIndexed(frameCtx.commandBuffer,
				drawRange.indexCount,
				1,
				drawRange.firstIndex,
				drawRange.vertexOffset,
				obj.instanceIndex
			);

			profiler.addDrawCall(drawRange.indexCount / 3);
		}
	}
}