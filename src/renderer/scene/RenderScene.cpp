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

	GPUShadowCSM _shadowCSM;
	GPUShadowCSM& getShadowCSM() { return _shadowCSM; }

	std::vector<GlobalInstance> _globalInstances;
	std::vector<glm::mat4> _globalTransforms;

	static Visibility::VisibilityState _visState;
	static std::vector<AABB> _visibleWorldAABBs;

	Camera _mainCamera;
	static glm::mat4 _curCamView;
	static glm::mat4 _curCamProj;

	const Camera getCamera() { return _mainCamera; }

	// Only wanna extract a new frustum if viewproj changes
	static glm::mat4 _lastViewProj = glm::mat4(1.0f);
	static glm::vec3 _lastLightDir = glm::vec3(0.0f);
	bool _isFirstViewProj = true;

	static Frustum _currentFrustum;

	static Frustum _cascadeFrustum;

	static void updateCamera();
	static void updateShadowCSM(const uint32_t shadowRes);

	static bool _assetsLoaded = false;
	bool _camChanged = false;
}

void RenderScene::setScene() {
	_mainCamera._velocity = glm::vec3(0.0f);
	_mainCamera._position = SPAWNPOINT;

	_mainCamera._pitch = 0;
	_mainCamera._yaw = -90.0f;

	_mainCamera._fovYDegrees = 70.0f;
	_mainCamera._nearClip = 0.1f;
	_mainCamera._farClip = 500.0f;

	_sceneData.sunlightColor = glm::vec4(1.0f, 0.96f, 0.87f, 2.5f);
	_sceneData.sunlightDirection = glm::vec4(-1.0f, 1.0f, 0.0f, 0.0f);
}

static void RenderScene::updateCamera() {
	const auto extent = Renderer::getDrawExtent();
	float width = static_cast<float>(extent.width);
	float height = static_cast<float>(extent.height);
	float aspect = width / height;

	_mainCamera.processInput(Engine::getWindow(), Engine::getProfiler());

	_curCamView = _mainCamera.getViewMatrix();
	_curCamProj = glm::perspectiveZO(glm::radians(_mainCamera._fovYDegrees), aspect, _mainCamera._nearClip, _mainCamera._farClip);
	_curCamProj[1][1] *= -1; // OpenGL style Y flip

	_sceneData.view = _curCamView;
	_sceneData.proj = _curCamProj;
	_sceneData.viewproj = _curCamProj * _curCamView;
	_sceneData.cameraPosition = glm::vec4(_mainCamera._position, _mainCamera._farClip);
	_sceneData.viewportSize = glm::vec4(width, height, 0.0f, 0.0f);

	if (_isFirstViewProj || _sceneData.viewproj != _lastViewProj) {
		_currentFrustum = Visibility::extractFrustum(_sceneData.viewproj);
		_lastViewProj = _sceneData.viewproj;
		_camChanged = true;
		_isFirstViewProj = false;
	}
	else {
		_camChanged = false;
	}
}

// This is busted, doesn't work
static void RenderScene::updateShadowCSM(const uint32_t shadowRes) {
	const glm::vec3 lightDir = glm::normalize(glm::vec3(_sceneData.sunlightDirection));

	const float eps = 1e-4f;
	bool lightChanged = !glm::all(glm::epsilonEqual(lightDir, _lastLightDir, eps));
	if (!_isFirstViewProj && !lightChanged && !_camChanged) return;

	_lastLightDir = lightDir;
	_lastViewProj = _sceneData.viewproj;

	constexpr float kShadowMaxDist = 140.0f;
	constexpr float kLambda        = 0.6f;
	constexpr float kPadXY         = 2.0f;
	constexpr float kPadZ          = 10.0f;
	constexpr float kLightDist     = 500.0f;

	const float nearClip = _mainCamera._nearClip;
	const float farClip = std::min(_mainCamera._farClip, kShadowMaxDist);
	const float fovYDeg = _mainCamera._fovYDegrees;
	const float aspect = _sceneData.viewportSize.x / _sceneData.viewportSize.y;

	// Compute split distances in view space (absolute units)
	std::array<float, MAX_CASCADES> splitDist{};
	for (uint32_t i = 0; i < MAX_CASCADES; ++i) {
		const float p = (static_cast<float>(i) + 1.0f) / static_cast<float>(MAX_CASCADES);
		const float logS = nearClip * std::pow(farClip / nearClip, p);
		const float uniS = nearClip + (farClip - nearClip) * p;
		splitDist[i] = kLambda * logS + (1.0f - kLambda) * uniS;
		_shadowCSM.cascadeSplits[i] = splitDist[i];
	}

	// light basis (Y is world-up)
	const glm::vec3 F = glm::normalize(lightDir);
	const glm::vec3 tmpUp = (std::abs(F.y) > 0.99f) ? glm::vec3(0,0,1) : glm::vec3(0,1,0);
	const glm::vec3 R = glm::normalize(glm::cross(tmpUp, F));
	const glm::vec3 U = glm::normalize(glm::cross(F, R));

	float lastSplit = nearClip;
	for (uint32_t i = 0; i < MAX_CASCADES; ++i) {
		const float curSplit = splitDist[i];

		// world-space corners of the slice
		Frustum slice = Visibility::extractCascadeFrustum(
			_curCamView,
			fovYDeg,
			aspect,
			lastSplit,
			curSplit);
		lastSplit = curSplit;

		// center in WS
		glm::vec3 centerWS(0.0f);
		for (int j = 0; j < 8; ++j) {
			centerWS += glm::vec3(slice.points[j]);
		}
		centerWS *= (1.0f / 8.0f);

		// light view
		const glm::vec3 eye = centerWS - F * kLightDist;
		const glm::mat4 lightView = glm::lookAtRH(eye, centerWS, U);

		// slice bounds in light space
		glm::vec2 minXY(+FLT_MAX), maxXY(-FLT_MAX);
		float minZ = +FLT_MAX, maxZ = -FLT_MAX;
		for (int j = 0; j < 8; ++j) {
			const glm::vec3 ls = glm::vec3(lightView * slice.points[j]);
			minXY = glm::min(minXY, glm::vec2(ls.x, ls.y));
			maxXY = glm::max(maxXY, glm::vec2(ls.x, ls.y));
			minZ = std::min(minZ, ls.z);
			maxZ = std::max(maxZ, ls.z);
		}

		// sphere-fit (square box) for stability
		const glm::vec2 ext = maxXY - minXY;
		const float radius = 0.5f * std::max(ext.x, ext.y);
		glm::vec3 centerLS(
			0.5f * (minXY.x + maxXY.x),
			0.5f * (minXY.y + maxXY.y),
			0.5f * (minZ + maxZ)
		);

		// snap center to texel grid
		const float worldPerTexel = (2.0f * radius) / static_cast<float>(shadowRes);
		centerLS.x = std::floor(centerLS.x / worldPerTexel) * worldPerTexel;
		centerLS.y = std::floor(centerLS.y / worldPerTexel) * worldPerTexel;

		// final ortho extents (square) + small padding
		const float left = (centerLS.x - radius) - kPadXY;
		const float right = (centerLS.x + radius) + kPadXY;
		const float bottom = (centerLS.y - radius) - kPadXY;
		const float top = (centerLS.y + radius) + kPadXY;

		// RH_ZO: positive distances along -Z
		const float nearD = std::max(0.001f, -maxZ - kPadZ);
		const float farD = std::max(nearD + 1.0f, -minZ + kPadZ);

		const glm::mat4 lightProj = glm::orthoRH_ZO(left, right, bottom, top, nearD, farD);
		_shadowCSM.cascadeVP[i] = lightProj * lightView;
	}
}

// Draw preparation work
void RenderScene::updateScene(FrameContext& frameCtx, GPUResources& gpuResources, const DebugToggles& debug) {
	_assetsLoaded = !_loadedScenes.empty();
	// === Update and draw scene ===

	updateCamera();

	const auto& shadowMap = ResourceManager::getShadowMapImage();
	// Define all csm parameters once
	if (_shadowCSM.params.y == 0.0f) {
		_shadowCSM.params.z = static_cast<float>(MAX_CASCADES);
		_shadowCSM.params.x = -0.001f; // shadow bias
		_shadowCSM.params.y = static_cast<float>(shadowMap.lutEntry.combinedImageIndex);
	}

	if (_assetsLoaded && debug.enableShadows) {
		updateShadowCSM(shadowMap.imageExtent.width);
	}

	const auto allocator = gpuResources.getAllocator();

	frameCtx.sceneDataBuffer = BufferUtils::createUniformBuffer(_sceneData, allocator);

	if (_assetsLoaded) {
		frameCtx.shadowCSMBuffer = BufferUtils::createUniformBuffer(_shadowCSM, allocator);
	}

	frameCtx.cpuDeletion.push_function([&, allocator]() mutable {
		BufferUtils::destroyAllocatedBuffer(frameCtx.sceneDataBuffer, allocator);
		if (_assetsLoaded) {
			BufferUtils::destroyAllocatedBuffer(frameCtx.shadowCSMBuffer, allocator);
		}
	});

	// No scene loaded in
	if (!_assetsLoaded) return;

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
		DrawPreparation::buildAndSortIndirectDraws(
			frameCtx,
			meshes,
			_visibleWorldAABBs,
			_sceneData.cameraPosition,
			debug);

		DrawPreparation::uploadGPUBuffersForFrame(frameCtx, gpuResources, _globalTransforms, Backend::getTransferQueue());
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