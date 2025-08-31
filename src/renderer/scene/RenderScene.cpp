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

	_sceneData.sunlightColor = glm::vec4(1.0f, 0.96f, 0.87f, 1.0f);
	_sceneData.sunlightDirection = glm::normalize(glm::vec4(1.0f, 1.0f, -0.787f, 0.0f));
}


void RenderScene::updateCamera() {
	const auto extent = Renderer::getDrawExtent();
	float width = static_cast<float>(extent.width);
	float height = static_cast<float>(extent.height);
	float aspect = width / height;

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
	_sceneData.viewportSize = glm::vec4(width, height, 0.0f, 0.0f);
}

// Draw preparation work
// Temporary, need a place to center ideas
void RenderScene::updateScene(FrameContext& frameCtx, GPUResources& gpuResources) {
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

	allocateSceneBuffer(frameCtx, gpuResources.getAllocator());

	// No scene loaded in
	if (_loadedScenes.empty()) return;

	auto& meshes = gpuResources.getResgisteredMeshes().meshData;

	DrawPreparation::syncGlobalInstancesAndTransforms(
		frameCtx,
		gpuResources,
		_sceneProfiles,
		_globalInstances,
		_globalTransforms);

	frameCtx.visSyncResult = Visibility::syncFromGlobalInstances(
		_visState,
		_globalInstances,
		_loadedScenes,
		meshes,
		_globalTransforms);

	Visibility::applySyncResult(
		_visState,
		frameCtx.visSyncResult);

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

		DrawPreparation::uploadGPUBuffersForFrame(frameCtx, gpuResources, _globalTransforms, Backend::getTransferQueue());
	}
}

void RenderScene::allocateSceneBuffer(FrameContext& frameCtx, const VmaAllocator allocator) {
	frameCtx.sceneDataBuffer = BufferUtils::createUniformBuffer(_sceneData, allocator);

	frameCtx.cpuDeletion.push_function([&, allocator]() mutable {
		BufferUtils::destroyAllocatedBuffer(frameCtx.sceneDataBuffer, allocator);
	});
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