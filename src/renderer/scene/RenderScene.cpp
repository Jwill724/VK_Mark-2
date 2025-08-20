#include "pch.h"

#include "RenderScene.h"
#include "SceneGraph.h"
#include "DrawPreparation.h"
#include "Visibility.h"
#include "core/Environment.h"
#include "utils/BufferUtils.h"
#include "engine/Engine.h"

namespace RenderScene {
	GPUSceneData _sceneData;
	GPUSceneData& getCurrentSceneData() { return _sceneData; }

	std::vector<GlobalInstance> _globalInstances;
	std::vector<glm::mat4> _globalTransforms;

	static Visibility::VisibilityState _visState;
	static std::vector<AABB> _visibleWorldAABBs;

	Camera _mainCamera;
	static glm::mat4 _curCamView;
	static glm::mat4 _curCamProj;

	const Camera getCamera() { return _mainCamera; }

	// Only wanna extract a new frustum if viewproj changes
	static glm::mat4 _lastViewProj;
	bool _isFirstViewProj = true;

	static Frustum _currentFrustum;
}

void RenderScene::setScene() {
	_mainCamera._velocity = glm::vec3(0.0f);
	_mainCamera._position = SPAWNPOINT;

	_mainCamera._pitch = 0;
	_mainCamera._yaw = -90.0f;

	_sceneData.ambientColor = glm::vec4(0.03f, 0.03f, 0.03f, 1.0f);
	_sceneData.sunlightColor = glm::vec4(1.0f, 0.96f, 0.87f, 2.5f);
	_sceneData.sunlightDirection = glm::normalize(glm::vec4(1.0f, 1.0f, -0.787f, 0.0f));
}


void RenderScene::updateCamera() {
	const auto extent = Renderer::getDrawExtent();
	float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);

	_mainCamera.processInput(Engine::getWindow(), Engine::getProfiler());

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

// Draw preparation work
// Temporary, need a place to center ideas
void RenderScene::updateScene(FrameContext& frameCtx, GPUResources& resources) {
	// === Update and draw scene ===

	updateCamera();

	// first frustum extracted to start chain of reuse
	if (_isFirstViewProj) {
		_lastViewProj = _sceneData.viewproj;
		_currentFrustum = Visibility::extractFrustum(_sceneData.viewproj);
		//copyFrustumToFrame(frameCtx.cullingPCData);
		_isFirstViewProj = false;
	}

	if (_sceneData.viewproj != _lastViewProj) {
		_lastViewProj = _sceneData.viewproj;
		_currentFrustum = Visibility::extractFrustum(_sceneData.viewproj);
		//copyFrustumToFrame(frameCtx.cullingPCData);
	}

	const auto allocator = resources.getAllocator();
	allocateSceneBuffer(frameCtx, allocator);

	// No scene loaded in
	if (_loadedScenes.empty()) return;

	auto& tQueue = Backend::getTransferQueue();
	auto& meshes = resources.getResgisteredMeshes().meshData;

	DrawPreparation::syncGlobalInstancesAndTransforms(
		frameCtx,
		resources,
		_sceneProfiles,
		_globalInstances,
		_globalTransforms,
		tQueue);

	frameCtx.visSyncResult = Visibility::syncFromGlobalInstances(
		_visState,
		_globalInstances,
		_loadedScenes,
		meshes,
		_globalTransforms);

	Visibility::applySyncResult(
		_visState,
		frameCtx.visSyncResult,
		meshes,
		_globalTransforms);

	// CPU CULLING
	frameCtx.clearRenderData();
	Visibility::cullBVHCollect(
		_visState,
		_currentFrustum,
		frameCtx.visibleInstances,
		_visibleWorldAABBs);

	if (!frameCtx.visibleInstances.empty()) {
		frameCtx.visibleCount = static_cast<uint32_t>(frameCtx.visibleInstances.size());

		DrawPreparation::buildAndSortIndirectDraws(frameCtx, meshes, _visibleWorldAABBs, _sceneData.cameraPosition);

		DrawPreparation::uploadGPUBuffersForFrame(frameCtx, tQueue);
	}
}

void RenderScene::allocateSceneBuffer(FrameContext& frameCtx, const VmaAllocator allocator) {
	frameCtx.sceneDataBuffer = BufferUtils::createBuffer(sizeof(GPUSceneData),
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, allocator);

	ASSERT(frameCtx.sceneDataBuffer.buffer != VK_NULL_HANDLE);
	ASSERT(frameCtx.sceneDataBuffer.mapped != nullptr);

	frameCtx.cpuDeletion.push_function([&, allocator]() mutable {
		BufferUtils::destroyAllocatedBuffer(frameCtx.sceneDataBuffer, allocator);
	});

	GPUSceneData* sceneDataPtr = reinterpret_cast<GPUSceneData*>(frameCtx.sceneDataBuffer.mapped);
	*sceneDataPtr = _sceneData;

	vmaFlushAllocation(allocator, frameCtx.sceneDataBuffer.allocation, 0, sizeof(GPUSceneData));
}

void RenderScene::renderGeometry(FrameContext& frameCtx, Profiler& profiler) {
	// all pipelines share push constant and descriptor setup
	auto defaultPC = Pipelines::_globalLayout.pcRange;

	// === SKYBOX DRAW ===
	{
		vkCmdBindPipeline(
			frameCtx.commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			Pipelines::getPipelineByID(PipelineID::Skybox));

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

	if (frameCtx.visibleCount == 0) return;

	auto& resources = Engine::getState().getGPUResources();

	drawIndirectCommands(frameCtx, resources, profiler);

	// === VISIBLE AABB FOR OBJECTS ===
	{
		if (profiler.debugToggles.showAABBs) {
			std::vector<glm::vec3> allVerts;
			std::vector<uint32_t> drawOffsets;

			auto emitAABBVerts = [&](const AABB& worldAABB) {
				auto verts = Visibility::GetAABBVertices(worldAABB);
				uint32_t offset = static_cast<uint32_t>(allVerts.size());
				drawOffsets.push_back(offset);
				allVerts.insert(allVerts.end(), verts.begin(), verts.end());
			};
			for (const auto& aabb : _visibleWorldAABBs) emitAABBVerts(aabb);

			const auto allocator = resources.getAllocator();

			const size_t totalSize = allVerts.size() * sizeof(glm::vec3);

			AllocatedBuffer aabbVBO = BufferUtils::createBuffer(
				totalSize,
				VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
				VMA_MEMORY_USAGE_CPU_TO_GPU,
				allocator);
			ASSERT(aabbVBO.info.pMappedData != nullptr);
			memcpy(aabbVBO.mapped, allVerts.data(), totalSize);

			auto aabbBuf = aabbVBO.buffer;
			auto aabbAlloc = aabbVBO.allocation;
			frameCtx.cpuDeletion.push_function([aabbBuf, aabbAlloc, allocator]() mutable {
				BufferUtils::destroyBuffer(aabbBuf, aabbAlloc, allocator);
			});

			vkCmdBindPipeline(
				frameCtx.commandBuffer,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				Pipelines::getPipelineByID(PipelineID::BoundingBox));

			const VkDeviceSize vtxOffset = 0;
			vkCmdBindVertexBuffers(frameCtx.commandBuffer, 0, 1, &aabbVBO.buffer, &vtxOffset);

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

void RenderScene::drawIndirectCommands(FrameContext& frameCtx, GPUResources& resources, Profiler& profiler) {
	auto pLayout = Pipelines::_globalLayout;

	const auto& idxBuffer = resources.getGPUAddrsBuffer(AddressBufferType::Index).buffer;

	// All pipelines use the same layout
	VkPipeline pipeline{};
	if (!profiler.pipeOverride.enabled)
		pipeline = Pipelines::getPipelineByID(PipelineID::Opaque); // default pipeline
	else
		pipeline = Pipelines::getPipelineByID(profiler.pipeOverride.selectedID);

	constexpr VkDeviceSize drawCmdSize = sizeof(VkDrawIndexedIndirectCommand);

	vkCmdBindPipeline(frameCtx.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	vkCmdBindIndexBuffer(frameCtx.commandBuffer, idxBuffer, 0, VK_INDEX_TYPE_UINT32);

	if (frameCtx.opaqueRange.visibleCount > 0) {

		vkCmdPushConstants(frameCtx.commandBuffer,
			pLayout.layout,
			pLayout.pcRange.stageFlags,
			pLayout.pcRange.offset,
			pLayout.pcRange.size,
			&frameCtx.drawDataPC);

		vkCmdDrawIndexedIndirect(frameCtx.commandBuffer,
			frameCtx.indirectDrawsBuffer.buffer,
			frameCtx.opaqueRange.first,
			frameCtx.opaqueRange.visibleCount,
			drawCmdSize
		);

		for (uint32_t i = 0; i < frameCtx.opaqueRange.visibleCount; ++i) {
			const auto& draw = frameCtx.indirectDraws[static_cast<size_t>(frameCtx.opaqueRange.first + i)];
			uint32_t triangleCount = (draw.indexCount * draw.instanceCount) / 3;
			profiler.addDrawCall(triangleCount);
		}
	}

	if (frameCtx.transparentRange.visibleCount > 0) {
		if (!profiler.pipeOverride.enabled) {
			vkCmdBindPipeline(
				frameCtx.commandBuffer,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				Pipelines::getPipelineByID(PipelineID::Transparent));
		}

		vkCmdPushConstants(frameCtx.commandBuffer,
			pLayout.layout,
			pLayout.pcRange.stageFlags,
			pLayout.pcRange.offset,
			pLayout.pcRange.size,
			&frameCtx.drawDataPC);

		vkCmdDrawIndexedIndirect(frameCtx.commandBuffer,
			frameCtx.indirectDrawsBuffer.buffer,
			frameCtx.transparentRange.first,
			frameCtx.transparentRange.visibleCount,
			drawCmdSize
		);

		const auto& meshes = resources.getResgisteredMeshes().meshData;

		for (uint32_t i = 0; i < frameCtx.transparentRange.visibleCount; ++i) {
			const uint32_t meshID = frameCtx.visibleInstances[static_cast<size_t>(frameCtx.transparentRange.first + i)].meshID;
			auto& mesh = meshes[meshID];
			uint32_t triangleCount = mesh.indexCount / 3;
			profiler.addDrawCall(triangleCount);
		}
	}
}

void RenderScene::copyFrustumToFrame(CullingPushConstantsAddrs& frustumData) {
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

void RenderScene::cleanScene() {
	_loadedScenes.clear();
	_visState.cleanup();
}