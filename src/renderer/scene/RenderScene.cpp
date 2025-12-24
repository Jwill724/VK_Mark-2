#include "pch.h"

#include "RenderScene.h"
#include "DrawPreparation.h"
#include "Visibility.h"
#include "core/Environment.h"
#include "utils/BufferUtils.h"
#include "engine/Engine.h"

namespace RenderScene {
	GPUSceneData _sceneData;
	GPUSceneData& getCurrentSceneData() { return _sceneData; }

	ShadowControl _shadowControl;

	GPUShadowCSM _shadowCSM;
	GPUShadowCSM& getShadowCSM() { return _shadowCSM; }

	std::vector<GlobalInstance> _globalInstances;
	std::vector<glm::mat4> _globalTransforms;
	std::vector<glm::mat4> _prevTransforms;

	bool _initializeTransformCopy = true;

	Visibility::VisibilityState _visState;
	std::vector<AABB> _visibleWorldAABBs;

	Camera _mainCamera;
	glm::mat4 _curCamView;
	glm::mat4 _curCamProj;

	const Camera& getCamera() { return _mainCamera; }

	// Only wanna extract a new frustum if viewproj changes
	glm::mat4 _lastViewProj = glm::mat4(1.0f);
	glm::vec3 _lastLightDir = glm::vec3(0.0f);

	float _cachedAspectRatio = 0.0f;

	Frustum _currentFrustum;

	void updateCamera();
	void updateShadowCSM(const glm::vec3& lightDir);
	void extendFrustumByLightDirection(Frustum& frus, const glm::vec3& lightDir, float extensionDist);
	bool _updateShadows = false;
	bool _shadowsOn = false;

	bool _assetsLoaded = false;
	bool _camChanged = false;

	bool _isTemporalInvalid = false;

	void createSceneBuffer(FrameContext& frameCtx, const VmaAllocator alloc);
}

void RenderScene::setScene(bool assetsLoaded) {
	_assetsLoaded = assetsLoaded;

	_mainCamera._velocity = glm::vec3(0.0f);
	_mainCamera._position = SPAWNPOINT;

	_mainCamera._pitch = 0.0f;
	_mainCamera._yaw = -90.0f;

	_mainCamera._fovY = 90.0f;
	_mainCamera._nearClip = 0.1f;
	_mainCamera._farClip = 1000.0f;

	_sceneData.cameraClips = glm::vec4(_mainCamera._nearClip, _mainCamera._farClip, 0.0f, 0.0f);

	_sceneData.sunlightColor = glm::vec4(1.0f, 0.55f, 0.2f, 2.5f);    // golden sun
	//_sceneData.sunlightColor = glm::vec4(1.0f, 0.96f, 0.87f, 2.5f); // white
	_sceneData.sunlightDirection = glm::vec4(0.36f, 0.68f, 0.125f, 0.0f);
}

void RenderScene::updateCamera() {
	const auto extent = Renderer::getDrawExtent();
	float width = static_cast<float>(extent.width);
	float height = static_cast<float>(extent.height);
	float aspect = width / height;

	_mainCamera.processInput(Engine::getWindow(), Engine::getProfiler(), _isTemporalInvalid);

	_curCamView = _mainCamera.getViewMatrix();
	_curCamProj = glm::perspectiveRH_ZO(glm::radians(_mainCamera._fovY), aspect, _mainCamera._farClip, _mainCamera._nearClip);

	_sceneData.view = _curCamView;
	_sceneData.proj = _curCamProj;
	_sceneData.prevViewproj = _lastViewProj;
	_sceneData.viewproj = _curCamProj * _curCamView;
	_sceneData.cameraPos = glm::vec4(_mainCamera._position, 0.0f);

	if (_sceneData.viewportSize.x != width || _sceneData.viewportSize.y != height) {
		float pixelCount = width * height;
		_sceneData.viewportSize = glm::vec4(width, height, pixelCount, 0.0f);

		_isTemporalInvalid = true;
	}

	if (_sceneData.viewproj != _lastViewProj) {
		_currentFrustum = Visibility::extractFrustum(_sceneData.viewproj);
		_lastViewProj = _sceneData.viewproj;
		_camChanged = true;

		_sceneData.invView = glm::inverse(_curCamView);
		_sceneData.invProj = glm::inverse(_curCamProj);
	}
	else {
		_camChanged = false;
	}
}

// Two great starting points to learn cascade shadow maps
// https://learnopengl.com/Guest-Articles/2021/CSM
// https://www.youtube.com/watch?v=3FMONJ1O39U&list=LL&index=157

// Both GLM_FORCE are enabled globally in hpp within pch
// #define GLM_FORCE_DEPTH_ZERO_TO_ONE
// #define GLM_FORCE_RIGHT_HANDED
// Pipeline depth compare LESS
// CULL MODE: FRONT BIT
void RenderScene::updateShadowCSM(const glm::vec3& lightDir) {
	const auto& shadowMap = ResourceManager::getShadowMapImage();
	const float shadowRes = static_cast<float>(shadowMap.extent.width);
	// Define all csm parameters once
	if (_shadowCSM.params.y == 0.0f) {
		_shadowControl.splitLambda = 0.97f;
		_shadowControl.lightDist = 0.25f;
		_shadowControl.bias = 0.0001f;
		_shadowCSM.params.z = static_cast<float>(MAX_SHADOW_CASCADES);
		_shadowCSM.params.y = static_cast<float>(shadowMap.lutEntry.combinedImageIndex);
		_shadowCSM.params.w = 1.0f / shadowRes;
		_shadowCSM.cascadeRadii = { 1.0f, 2.0f, 3.0f, 5.0f };
	}

	const float aspect = _sceneData.viewportSize.x / _sceneData.viewportSize.y;

	if (_cachedAspectRatio != aspect) {
		const float nearClip = _mainCamera._nearClip;
		const float farClip = _mainCamera._farClip;
		const float clipRange = farClip - nearClip;
		const float ratio = farClip / nearClip;

		// Compute split distances in view space (absolute units)
		for (uint32_t i = 0; i < MAX_SHADOW_CASCADES; ++i) {
			const float p = (static_cast<float>(i) + 1.0f) / static_cast<float>(MAX_SHADOW_CASCADES);
			const float log = nearClip * std::pow(ratio, p);
			const float uni = nearClip + (clipRange * p);
			_shadowCSM.cascadeSplits[i] = (_shadowControl.splitLambda * log) + ((1.0f - _shadowControl.splitLambda) * uni);
		}
	}

	float lastSplitDist = _mainCamera._nearClip;
	for (uint32_t i = 0; i < MAX_SHADOW_CASCADES; ++i) {
		const float curSplit = _shadowCSM.cascadeSplits[i];

		// world-space corners of the slice
		glm::mat4 proj = glm::perspective(glm::radians(_mainCamera._fovY), aspect, lastSplitDist, curSplit);
		glm::mat4 invVp = glm::inverse(proj * _curCamView);

		// Zero to one depth
		glm::vec4 frustumCorners[8] = {
			{ -1.0f,  1.0f, 0.0f, 1.0f }, // near
			{  1.0f,  1.0f, 0.0f, 1.0f },
			{  1.0f, -1.0f, 0.0f, 1.0f },
			{ -1.0f, -1.0f, 0.0f, 1.0f },
			{ -1.0f,  1.0f, 1.0f, 1.0f }, // far
			{  1.0f,  1.0f, 1.0f, 1.0f },
			{  1.0f, -1.0f, 1.0f, 1.0f },
			{ -1.0f, -1.0f, 1.0f, 1.0f }
		};

		glm::vec3 frustumCenter(0.0f);
		for (auto& v : frustumCorners) {
			glm::vec4 cornerWorld = invVp * v;
			v = cornerWorld / cornerWorld.w;
			frustumCenter += glm::vec3(v);
		}

		// center in world space
		frustumCenter /= 8.0f;

		float radius = 0.0f;
		for (const auto& v : frustumCorners) {
			float distance = glm::length(glm::vec3(v) - frustumCenter);
			radius = glm::max(radius, distance);
		}
		radius = std::ceil(radius * 16.0f) / 16.0f;

		glm::vec3 max = glm::vec3(radius);
		glm::vec3 min = -max;

		// Light view
		const glm::vec3 lightPos = frustumCenter + lightDir;
		const glm::mat4 lightView = glm::lookAt(lightPos, frustumCenter, glm::vec3(0.0f, 1.0f, 0.0f));

		// Extend depth range
		// The scale factors cover edge cases for stability,
		// depending on distance with the occluder and the view.
		const float depthRange = max.z - min.z;
		min.z -= depthRange * 5.0f;
		max.z += depthRange * 2.0f;

		// Orthographic projection
		glm::mat4 lightProj = glm::orthoRH_ZO(min.x, max.x, min.y, max.y, min.z, max.z);
		glm::mat4 shadowMatrix = lightProj * lightView;

		// This works beautifully, it keeps the shadows 100% stable during movement
		// https://github.com/tonadr1022/vkrender2/blob/main/src/techniques/CSM.cpp
		// scale origin by shadow map size
		// round it (nearest texel)
		// get the offset
		// scale it back down, only use x,y and apply it to vp matrix
		glm::vec3 shadowOrigin = shadowMatrix * glm::vec4(glm::vec3(0.0f), 1.0f);
		shadowOrigin = shadowOrigin * shadowRes / 2.0f;
		glm::vec3 roundedOrigin = glm::round(shadowOrigin);
		glm::vec3 roundOffset = roundedOrigin - shadowOrigin;
		roundOffset = roundOffset * 2.0f / shadowRes;
		roundOffset.z = 0.0f;
		shadowMatrix[3] += glm::vec4(roundOffset, 0.0f);
		_shadowCSM.cascadeVP[i] = shadowMatrix;

		lastSplitDist = curSplit;
	}
}

void RenderScene::createSceneBuffer(FrameContext& frameCtx, const VmaAllocator alloc) {
	frameCtx.sceneDataBuffer = BufferUtils::createUniformBuffer(_sceneData, alloc);

	frameCtx.cpuDeletion.push_function([&, alloc]() mutable {
		BufferUtils::destroyAllocatedBuffer(frameCtx.sceneDataBuffer, alloc);
	});
}

void RenderScene::updateScene(FrameContext& frameCtx, GPUResources& gpuResources, const DebugToggles& debug) {
	_isTemporalInvalid = false; // Assume clean start each frame

	updateCamera();

	const auto allocator = gpuResources.getAllocator();

	// No scene loaded in
	if (!_assetsLoaded) {
		_sceneData.temporal.x = frameCtx.frameIndex;
		_sceneData.temporal.y = 0u;
		createSceneBuffer(frameCtx, allocator);
		return;
	}

	auto& meshes = gpuResources.getResgisteredMeshes().meshData;

	// Start of each frame copy the current transforms into previous.
	// Frame 0 this will just be empty.
	_prevTransforms = _globalTransforms;

	DrawPreparation::syncGlobalInstancesAndTransforms(
		frameCtx,
		gpuResources,
		_sceneProfiles,
		_globalInstances,
		_globalTransforms);

	// The command for this upload is uploaded during frame 0 initialization upload for transforms,
	// Including all previous asset global loaded buffers.
	// Could also occur if any buffer is destoryed internally.
	gpuResources.updateAddressTableMapped();

	// First time intialization copy
	if (_initializeTransformCopy) {
		_prevTransforms = _globalTransforms;
		_initializeTransformCopy = false;
		_isTemporalInvalid = true;
	}

	// Instances with transforms counts could be increases or shrunk during the sync
	if (_globalTransforms.size() != _prevTransforms.size()) {
		_isTemporalInvalid = true;
		_prevTransforms = _globalTransforms;
	}

	frameCtx.visSyncResult = Visibility::syncFromGlobalInstances(
		_visState,
		_globalInstances,
		_loadedScenes,
		meshes,
		_globalTransforms);

	Visibility::applySyncResult(
		_visState,
		frameCtx.visSyncResult);

	frameCtx.clearRenderData();

	const glm::vec3 lightDir = glm::normalize(glm::vec3(_sceneData.sunlightDirection));
	if (debug.enableShadows) {
		_shadowCSM.params.x = _shadowControl.bias;

		if (_camChanged || (lightDir != _lastLightDir) || !_shadowsOn) {
			if (!_camChanged && lightDir != _lastLightDir) {
				// Extract new frustum if only light has changed
				_currentFrustum = Visibility::extractFrustum(_sceneData.viewproj);
				_lastLightDir = lightDir;
				_lastViewProj = _sceneData.viewproj;
			}
			float extensionDist = _mainCamera._farClip * _shadowControl.lightDist;
			extendFrustumByLightDirection(_currentFrustum, lightDir, extensionDist);
			_updateShadows = true;
			_shadowsOn = true;
		}
		else {
			_updateShadows = false;
		}
	}
	// In event that view or light has changed and light is off,
	else if (_shadowsOn) {
		_lastViewProj = glm::mat4(1.0f); // Default viewproj to recalculate frustum
		_shadowsOn = false;
	}

	// CPU CULLING
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
			_sceneData.cameraPos,
			debug);

		DrawPreparation::uploadGPUBuffersForFrame(
			frameCtx,
			gpuResources,
			_globalTransforms,
			_prevTransforms,
			Backend::getTransferQueue());

		if (debug.enableShadows && _updateShadows) {
			updateShadowCSM(lightDir);
		}
	}

	_sceneData.temporal.x = frameCtx.frameIndex;
	if (debug.enableTemporal) {
		_sceneData.temporal.y = _isTemporalInvalid ? 0u : 1u;
	}
	else {
		_sceneData.temporal.y = 0u;
	}
	createSceneBuffer(frameCtx, allocator);

	// Vulkan requires a buffer created once its defined in used shader, even if that buffer isn't actually used.
	frameCtx.shadowCSMBuffer = BufferUtils::createUniformBuffer(_shadowCSM, allocator);
	frameCtx.cpuDeletion.push_function([&, allocator]() mutable {
		BufferUtils::destroyAllocatedBuffer(frameCtx.shadowCSMBuffer, allocator);
	});
}

// To improve shadow casters that appear out of the main view frustum
void RenderScene::extendFrustumByLightDirection(Frustum& frus, const glm::vec3& lightDir, float extensionDist) {
	for (uint32_t i = 0; i < 6; ++i) {
		glm::vec3 normalPlane = glm::vec3(frus.planes[i]);
		float facing = glm::dot(normalPlane, lightDir);

		// Plane faces roughly toward light
		if (facing < 0.0f) {
			// Move it outward along its normal
			frus.planes[i].w += extensionDist * (-facing);
		}
	}
}

void RenderScene::cleanScene(GPUAddressTable& globalTable) {
	_loadedScenes.clear();
	_visState.cleanup();
	_globalTransforms.clear();
	_prevTransforms.clear();
	globalTable.removeAddress(AddressBufferType::Transforms);
	globalTable.removeAddress(AddressBufferType::PrevTransforms);
	_initializeTransformCopy = true;
}