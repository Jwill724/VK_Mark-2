#include "pch.h"

#include "RenderScene.h"
#include "DrawPreparation.h"
#include "Visibility.h"
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
	uint32_t _recentTransformCount = 0;

	bool _initializeTransformCopy = true;

	Visibility::VisibilityState _visState;
	std::vector<AABB> _visibleWorldAABBs;

	Frustum _cascadeFrustums[MAX_SHADOW_CASCADES];

	Camera _mainCamera;
	glm::mat4 _curCamView;
	glm::mat4 _curCamProj;

	bool _lastFlashLightActive = false;
	bool _flashLightDirtyAllFrames = false;
	uint32_t _lightStateVersion = 0u;

	Camera& getCamera() { return _mainCamera; }

	glm::mat4 _curCamProjUnjittered = glm::mat4(1.0f);
	const glm::mat4& getCurProjUnjittered() { return _curCamProjUnjittered; }

	glm::mat4 _curCamProjJittered = glm::mat4(1.0f);

	glm::mat4 _lastViewProjUnjittered = glm::mat4(1.0f);
	glm::mat4 _lastViewProjJittered = glm::mat4(1.0f);

	glm::vec2 _currentJitterNDC = glm::vec2(0.0f);
	glm::vec2 _previousJitterNDC = glm::vec2(0.0f);

	float _shadowFar = 800.0f;

	float _cachedAspectRatio = 0.0f;

	glm::mat4 _lastView = glm::mat4(1.0f);

	Frustum _currentFrustum;
	const Frustum& getMainFrustum() { return _currentFrustum; }

	void updateCamera();
	void updateShadowCSM(const glm::vec3& lightDir);

	bool _assetsLoaded = false;

	bool _isTemporalInvalid = false;

	void createSceneBuffer(FrameContext& frameCtx, const VmaAllocator alloc);

	void updateDrawDataCPUPath(
		FrameContext& frameCtx,
		GPUResources& gpuResources,
		const DebugToggles& debug);
	void updateDrawDataGPUPath(FrameContext& frameCtx, GPUResources& gpuResources);

	enum RenderPath : uint32_t {
		CPU,
		GPU
	};
	RenderPath _currentPath = RenderPath::CPU;

	DispatchList _dispatchListSSS;

	DispatchList buildDispatchList(
		const glm::vec4 lightProj,
		const glm::vec2 viewportSize,
		const int waveSize = 64
	);

	void initCSMAtlasUVs();
}

static float haltonSequence(uint32_t index, uint32_t base)
{
	float f = 1.0, r = 0.0;
	while (index > 0) {
		f /= static_cast<float>(base);
		r += f * float(index % base);
		index /= base;
	}
	return r;
}

static glm::vec2 buildTemporalJitterPixels(uint32_t frameIndex)
{
	const uint32_t sequenceLength = 16u;
	uint32_t index = (frameIndex % sequenceLength) + 1u;

	glm::vec2 jitter;
	jitter.x = haltonSequence(index, 2u);
	jitter.y = haltonSequence(index, 3u);
	jitter -= glm::vec2(0.5f);
	return jitter;
}

static glm::vec2 convertJitterPixelsToNDC(
	const glm::vec2 jitterPixels,
	const float width,
	const float height)
{
	glm::vec2 jitterNDC = glm::vec2(0.0f);

	jitterNDC.x = (2.0f * jitterPixels.x) / width;
	jitterNDC.y = (2.0f * jitterPixels.y) / height;

	return jitterNDC;
}

static glm::mat4 applyProjectionJitter(
	glm::mat4 proj,
	const glm::vec2 jitterNDC)
{
	proj[2][0] += jitterNDC.x;
	proj[2][1] += jitterNDC.y;
	return proj;
}

void RenderScene::setScene(bool assetsLoaded) {
	_assetsLoaded = assetsLoaded;

	_mainCamera.setPosition(SPAWNPOINT);
	_mainCamera.setYaw(-90.0f);
	_mainCamera.setFovY(90.0f);

	_mainCamera.setSensitivity(50.0f); // Feels good on 1600dpi
	_mainCamera.setMaxSpeed(20.0f);
	_mainCamera.setMinSpeed(5.0f);

	_mainCamera.setAcceleration(20.0f);
	_mainCamera.setDamping(8.0f);

	_sceneData.cameraClips = glm::vec4(_mainCamera.getNearClip(), _mainCamera.getFarClip(), 0.0f, 0.0f);

	_currentJitterNDC = glm::vec2(0.0f);
	_previousJitterNDC = glm::vec2(0.0f);

	_lastViewProjUnjittered = glm::mat4(1.0f);
	_lastViewProjJittered = glm::mat4(1.0f);

	_sceneData.sunlightColor = glm::vec4(1.0f, 0.55f, 0.2f, 2.5f);    // golden sun
	//_sceneData.sunlightColor = glm::vec4(1.0f, 0.96f, 0.87f, 2.5f); // white
	_sceneData.sunlightDirection = glm::vec4(0.36f, 0.46f, -0.09f, 0.0f);
}

void RenderScene::updateCamera() {
	const auto extent = Renderer::getDrawExtent();
	float width  = static_cast<float>(extent.width);
	float height = static_cast<float>(extent.height);
	float aspect = width / height;

	auto& profiler = Engine::getProfiler();
	_mainCamera.processInput(Engine::getWindow(), profiler, _isTemporalInvalid);

	_curCamView = _mainCamera.getViewMatrix();

	_curCamProjUnjittered = glm::perspectiveRH_ZO(
		glm::radians(_mainCamera.getFovY()),
		aspect,
		_mainCamera.getFarClip(),
		_mainCamera.getNearClip());

	_previousJitterNDC = _currentJitterNDC;

	glm::vec2 currentJitterPixels = buildTemporalJitterPixels(_sceneData.temporal.x);
	_currentJitterNDC = convertJitterPixelsToNDC(currentJitterPixels, width, height);
	_curCamProjJittered = applyProjectionJitter(_curCamProjUnjittered, _currentJitterNDC);

	// Compute both unjittered and jittered viewprojs up front
	glm::mat4 currentViewProjUnjittered = _curCamProjUnjittered * _curCamView;
	glm::mat4 currentViewProjJittered = _curCamProjJittered * _curCamView;

	_sceneData.view = _curCamView;
	_sceneData.invView = glm::inverse(_curCamView);

	if (profiler.debugToggles.aaMode == AA_TAA) {
		_sceneData.proj = _curCamProjJittered;
		_sceneData.invProj = glm::inverse(_curCamProjJittered);
		_sceneData.viewProj = currentViewProjJittered;
	}
	else {
		_sceneData.proj = _curCamProjUnjittered;
		_sceneData.invProj = glm::inverse(_curCamProjUnjittered);
		_sceneData.viewProj = currentViewProjUnjittered;
	}

	_sceneData.viewProjUnjittered = currentViewProjUnjittered; // unjittered current — velocity curr NDC

	_sceneData.prevViewProj = _lastViewProjUnjittered;         // unjittered previous — velocity prev NDC

	_sceneData.cameraPos = glm::vec4(_mainCamera.getPosition(), 0.0f);
	_sceneData.temporalJitter = glm::vec4(
		_currentJitterNDC.x,
		_currentJitterNDC.y,
		_previousJitterNDC.x,
		_previousJitterNDC.y);

	_currentFrustum = extractFrustum(currentViewProjUnjittered);
	_lastViewProjUnjittered = currentViewProjUnjittered;
	_lastViewProjJittered = currentViewProjJittered;

	_sceneData.prevView = _lastView;
	_lastView = _curCamView;

	if (_sceneData.viewportSize.x != width || _sceneData.viewportSize.y != height) {
		float pixelCount = width * height;
		_sceneData.viewportSize = glm::vec4(width, height, pixelCount, 0.0f);

		uint32_t widthU  = static_cast<uint32_t>(width);
		uint32_t heightU = static_cast<uint32_t>(height);

		glm::vec2 fullPixelSize = 1.0f / glm::vec2(width, height);
		_sceneData.cameraClips.z = fullPixelSize.x;
		_sceneData.cameraClips.w = fullPixelSize.y;

		VkExtent3D halfExtent = {
			(widthU + 1u) >> 1,
			(heightU + 1u) >> 1,
			1u
		};

		glm::vec2 halfPixelSize = 1.0f /
			glm::vec2(
				static_cast<float>(halfExtent.width),
				static_cast<float>(halfExtent.height));

		_sceneData.pixelSizes = glm::vec4(
			fullPixelSize.x,
			fullPixelSize.y,
			halfPixelSize.x,
			halfPixelSize.y);

		_isTemporalInvalid = true;
	}
}

static inline glm::vec4 buildAtlasUV(
	VkExtent2D atlasExtent,
	VkExtent2D tileExtent,
	uint32_t tileX,
	uint32_t tileY,
	uint32_t borderPixels)
{
	const float atlasW = static_cast<float>(atlasExtent.width);
	const float atlasH = static_cast<float>(atlasExtent.height);

	const float tileW  = static_cast<float>(tileExtent.width);
	const float tileH  = static_cast<float>(tileExtent.height);

	const float borderU = static_cast<float>(borderPixels) / atlasW;
	const float borderV = static_cast<float>(borderPixels) / atlasH;

	const float offsetX = static_cast<float>(tileX) * tileW;
	const float offsetY = static_cast<float>(tileY) * tileH;

	const float offsetU = (offsetX / atlasW) + borderU;
	const float offsetV = (offsetY / atlasH) + borderV;

	const float scaleU  = (tileW / atlasW) - 2.0f * borderU;
	const float scaleV  = (tileH / atlasH) - 2.0f * borderV;

	return glm::vec4(scaleU, scaleV, offsetU, offsetV);
}

void RenderScene::initCSMAtlasUVs()
{
	const auto& atlas = ResourceManager::getDirectionalCSMAtlas();

	const VkExtent2D atlasExtent = {
		atlas.extent.width,
		atlas.extent.height
	};

	// For a 2x2 grid:
	const uint32_t tilesPerRow = 2u;
	const VkExtent2D tileExtent = {
		atlas.extent.width / tilesPerRow,
		atlas.extent.height / tilesPerRow
	};

	const uint32_t borderPixels = 2;

	for (uint32_t cascadeIndex = 0; cascadeIndex < MAX_SHADOW_CASCADES; ++cascadeIndex) {
		const uint32_t tileX = cascadeIndex % tilesPerRow;
		const uint32_t tileY = cascadeIndex / tilesPerRow;

		_shadowCSM.atlasUV[cascadeIndex] =
			buildAtlasUV(atlasExtent, tileExtent, tileX, tileY, borderPixels);
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
	const auto& shadowAtlas = ResourceManager::getDirectionalCSMAtlas();
	const float shadowResWidth = static_cast<float>(shadowAtlas.extent.width);
	const float tileRes = static_cast<float>(shadowResWidth / 2u);
	// Define all csm parameters once
	if (_shadowCSM.params.y == 0.0f) {
		_shadowControl.splitLambda = 0.97f;
		_shadowControl.bias = 0.0001f;
		_shadowCSM.params.z = static_cast<float>(MAX_SHADOW_CASCADES);
		_shadowCSM.params.y = static_cast<float>(shadowAtlas.lutEntry.combinedImageIndex);
		_shadowCSM.params.w = 1.0f / tileRes;
		_shadowCSM.maxFilterRadiusTexels = { 1.0f, 1.1f, 1.2f, 1.5f };
		initCSMAtlasUVs();
	}

	const float aspect = _sceneData.viewportSize.x / _sceneData.viewportSize.y;

	if (_cachedAspectRatio != aspect) {
		_cachedAspectRatio = aspect;

		const float nearClip = _mainCamera.getNearClip();
		const float farClip = _shadowFar;
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

	float lastSplitDist = _mainCamera.getNearClip();
	for (uint32_t i = 0; i < MAX_SHADOW_CASCADES; ++i) {
		const float curSplit = _shadowCSM.cascadeSplits[i];

		// world-space corners of the slice
		glm::mat4 proj = glm::perspective(glm::radians(_mainCamera.getFovY()), aspect, lastSplitDist, curSplit);
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

		// These two scale factors keep shadows working
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
		shadowOrigin = shadowOrigin * tileRes / 2.0f;
		glm::vec3 roundedOrigin = glm::round(shadowOrigin);
		glm::vec3 roundOffset = roundedOrigin - shadowOrigin;
		roundOffset = roundOffset * 2.0f / tileRes;
		roundOffset.z = 0.0f;
		shadowMatrix[3] += glm::vec4(roundOffset, 0.0f);
		_shadowCSM.cascadeVP[i] = shadowMatrix;

		lastSplitDist = curSplit;

		_cascadeFrustums[i] = extractFrustum(_shadowCSM.cascadeVP[i]);
	}
}


static int bend_min(const int a, const int b) { return a > b ? b : a; }
static int bend_max(const int a, const int b) { return a > b ? a : b; }

// Dispatch building logic based on Bend Studio's
// https://www.bendstudio.com/blog/inside-bend-screen-space-shadows/
DispatchList RenderScene::buildDispatchList(
	const glm::vec4 lightProj,
	const glm::vec2 viewportSize,
	const int waveSize)
{
	DispatchList result;

	// Floating point division in the shader has a practical limit for precision when the light is *very* far off screen (~1m pixels+)
	// So when computing the light XY coordinate, use an adjusted w value to handle these extreme values
	float xy_light_w = lightProj[3];
	const float FP_limit = 0.000002f * static_cast<float>(waveSize);

	if (xy_light_w >= 0 && xy_light_w < FP_limit) xy_light_w = FP_limit;
	else if (xy_light_w < 0 && xy_light_w > -FP_limit) xy_light_w = -FP_limit;

	// Need precise XY pixel coordinates of the light
	result.lightCoords[0] = ((lightProj[0] / xy_light_w) * +0.5f + 0.5f) * viewportSize.x;

	// NOTE: Y flip required for my light projection to work
	result.lightCoords[1] = (1.0f - ((lightProj[1] / xy_light_w) * -0.5f + 0.5f)) * viewportSize.y;
	result.lightCoords[2] = lightProj[3] == 0 ? 0 : (lightProj[2] / lightProj[3]);
	result.lightCoords[3] = lightProj[3] > 0 ? 1.0f : -1.0f;

	int32_t light_xy[2] =
	{
		static_cast<int32_t>((result.lightCoords[0] + 0.5f)),
		static_cast<int32_t>((result.lightCoords[1] + 0.5f))
	};

	// Make the bounds inclusive, relative to the light
	const int32_t biased_bounds[4] =
	{
		0 - light_xy[0],
		-(static_cast<int32_t>(viewportSize.y) - light_xy[1]),
		static_cast<int32_t>(viewportSize.x) - light_xy[0],
		-(0 - light_xy[1]),
	};

	// Process 4 quadrants around the light center,
	// They each form a rectangle with one corner on the light XY coordinate
	// If the rectangle isn't square, it will need breaking in two on the larger axis
	// 0 = bottom left, 1 = bottom right, 2 = top left, 2 = top right
	for (int q = 0; q < 4; q++) {
		// Quads 0 and 3 needs to be +1 vertically, 1 and 2 need to be +1 horizontally
		bool vertical = q == 0 || q == 3;

		// Bounds relative to the quadrant
		const int bounds[4] =
		{
			bend_max(0, ((q & 1) ? biased_bounds[0] : -biased_bounds[2])) / waveSize,
			bend_max(0, ((q & 2) ? biased_bounds[1] : -biased_bounds[3])) / waveSize,
			bend_max(0, (((q & 1) ? biased_bounds[2] : -biased_bounds[0]) + waveSize * (vertical ? 1 : 2) - 1)) / waveSize,
			bend_max(0, (((q & 2) ? biased_bounds[3] : -biased_bounds[1]) + waveSize * (vertical ? 2 : 1) - 1)) / waveSize,
		};

		if ((bounds[2] - bounds[0]) > 0 && (bounds[3] - bounds[1]) > 0) {
			int bias_x = (q == 2 || q == 3) ? 1 : 0;
			int bias_y = (q == 1 || q == 3) ? 1 : 0;

			DispatchData& disp = result.dispatch[result.dispatchCount++];

			disp.waveCount[0] = waveSize; // 64
			disp.waveCount[1] = bounds[2] - bounds[0];
			disp.waveCount[2] = bounds[3] - bounds[1];
			disp.waveOffset[0] = ((q & 1) ? bounds[0] : -bounds[2]) + bias_x;
			disp.waveOffset[1] = ((q & 2) ? -bounds[3] : bounds[1]) + bias_y;

			// We want the far corner of this quadrant relative to the light,
			// as we need to know where the diagonal light ray intersects with the edge of the bounds
			int axis_delta = +biased_bounds[0] - biased_bounds[1];
			if (q == 1) axis_delta = +biased_bounds[2] + biased_bounds[1];
			if (q == 2) axis_delta = -biased_bounds[0] - biased_bounds[3];
			if (q == 3) axis_delta = -biased_bounds[2] + biased_bounds[3];

			axis_delta = (axis_delta + waveSize - 1) / waveSize;

			if (axis_delta > 0)
			{
				DispatchData& disp2 = result.dispatch[result.dispatchCount++];

				// Take copy of current volume
				disp2 = disp;

				if (q == 0)
				{
					// Split on Y, split becomes -1 larger on x
					disp2.waveCount[2] = bend_min(disp.waveCount[2], axis_delta);
					disp.waveCount[2] -= disp2.waveCount[2];
					disp2.waveOffset[1] = disp.waveOffset[1] + disp.waveCount[2];
					disp2.waveOffset[0]--;
					disp2.waveCount[1]++;
				}
				if (q == 1)
				{
					// Split on X, split becomes +1 larger on y
					disp2.waveCount[1] = bend_min(disp.waveCount[1], axis_delta);
					disp.waveCount[1] -= disp2.waveCount[1];
					disp2.waveOffset[0] = disp.waveOffset[0] + disp.waveCount[1];
					disp2.waveCount[2]++;
				}
				if (q == 2)
				{
					// Split on X, split becomes -1 larger on y
					disp2.waveCount[1] = bend_min(disp.waveCount[1], axis_delta);
					disp.waveCount[1] -= disp2.waveCount[1];
					disp.waveOffset[0] += disp2.waveCount[1];
					disp2.waveCount[2]++;
					disp2.waveOffset[1]--;
				}
				if (q == 3)
				{
					// Split on Y, split becomes +1 larger on x
					disp2.waveCount[2] = bend_min(disp.waveCount[2], axis_delta);
					disp.waveCount[2] -= disp2.waveCount[2];
					disp.waveOffset[1] += disp2.waveCount[2];
					disp2.waveCount[1]++;
				}

				// Remove if too small
				if (disp2.waveCount[1] <= 0 || disp2.waveCount[2] <= 0)
				{
					disp2 = result.dispatch[--result.dispatchCount];
				}
				if (disp.waveCount[1] <= 0 || disp.waveCount[2] <= 0)
				{
					disp = result.dispatch[--result.dispatchCount];
				}
			}
		}
	}

	// Scale the shader values by the wave count, the shader expects this
	for (int i = 0; i < result.dispatchCount; i++) {
		result.dispatch[i].waveOffset[0] *= waveSize;
		result.dispatch[i].waveOffset[1] *= waveSize;
	}

	return result;
}

void RenderScene::createSceneBuffer(FrameContext& frameCtx, const VmaAllocator alloc) {
	frameCtx.sceneData_UBO = BufferUtils::createUniformBuffer(_sceneData, alloc);

	frameCtx.cpuDeletion.push_function([&, alloc]() mutable {
		BufferUtils::destroyAllocatedBuffer(frameCtx.sceneData_UBO, alloc);
	});
}

void RenderScene::updateScene(
	FrameContext& frameCtx,
	GPUResources& gpuResources,
	const DebugToggles& debug)
{
	_isTemporalInvalid = false; // Assume clean start each frame

	_sceneData.temporal.x = frameCtx.frameIndex;

	updateCamera();

	const auto allocator = gpuResources.getAllocator();

	// No scene loaded in
	if (!_assetsLoaded) {
		_sceneData.temporal.y = 0u;
		createSceneBuffer(frameCtx, allocator);
		return;
	}

	frameCtx.clearRenderData();

	const auto deltaTime = Engine::getProfiler().getStats().deltaSecondsRaw;

	// Light Updates, handle dynamics first
	bool mainList = false;
	bool dynamicList = false;
	bool flashLightChanged = false;

	if (UserInput::keyboard.isPressed(GLFW_KEY_F)) {
		LightingSystem::_mainFlashLight.toggleFlashLight();
	}

	const bool flashLightActive = LightingSystem::_mainFlashLight.isFlashLightActive();

	flashLightChanged = LightingSystem::_mainFlashLight.updateFlashLight(
		LightingSystem::_globalLightList,
		ResourceManager::getFlashLightShadowMap().lutEntry.combinedImageIndex,
		ResourceManager::getCookieGoboImage().lutEntry.combinedImageIndex,
		_mainCamera.getPosition(),
		_mainCamera.getView(),
		deltaTime,
		_mainCamera.getDelta(),
		_mainCamera.getView()
	);

	bool flashLightStateChanged = false;
	if (flashLightActive != _lastFlashLightActive) {
		_lastFlashLightActive = flashLightActive;
		flashLightStateChanged = true;
	}

	if (flashLightChanged || flashLightStateChanged) {
		++_lightStateVersion;
	}

	if (frameCtx.uploadedFlashLightVersion != _lightStateVersion) {
		frameCtx.lightsBufferUploadNeeded = true;
		frameCtx.uploadedFlashLightVersion = _lightStateVersion;
	}

	mainList = LightingSystem::updateLightList();
	if (LightingSystem::_dynamicLightsEnabled) {
		dynamicList = LightingSystem::updateDynamicLightsOrbit(deltaTime);
		frameCtx.recentDynamicLightsTransform = true;
	}
	else {
		// Requires update when count hasn't changed but dynamic and static states
		if (frameCtx.recentDynamicLightsTransform && !frameCtx.lightsBufferUploadNeeded) {
			frameCtx.lightsBufferUploadNeeded = true;
			frameCtx.recentDynamicLightsTransform = false;
		}
	}

	// Static update changes
	if (frameCtx.recentLightListCount != LightingSystem::_globalLightList.size()) {
		frameCtx.recentLightListCount = static_cast<uint32_t>(LightingSystem::_globalLightList.size());
		frameCtx.lightsBufferUploadNeeded = true;
	}

	// First time init for lights buffer
	if (!frameCtx.lightsInitialized && (mainList || dynamicList || flashLightChanged)) {
		frameCtx.lightsInitialized = true;
	}

	if (mainList || dynamicList || flashLightChanged) {
		frameCtx.lightsBufferUploadNeeded = true;
	}

	frameCtx.transformsBufferUploadNeeded = DrawPreparation::syncGlobalInstancesAndTransforms(
		_sceneProfiles,
		_globalInstances,
		_globalTransforms,
		deltaTime);

	// First time upload for transforms
	if (!frameCtx.transformsInitialized) {
		frameCtx.transformsInitialized = true;
		frameCtx.transformsBufferUploadNeeded = true;
	}

	// First time intialization copy
	if (_initializeTransformCopy) {
		_initializeTransformCopy = false;
		_isTemporalInvalid = true;
	}

	// Instances with transforms counts could be increased or shrunk during the sync
	if (static_cast<uint32_t>(_globalTransforms.size()) != _recentTransformCount) {
		_isTemporalInvalid = true;
		_recentTransformCount = static_cast<uint32_t>(_globalTransforms.size());
	}

	const glm::vec3 lightDir = glm::normalize(glm::vec3(_sceneData.sunlightDirection));

	// Screen space contact shadows
	if (debug.enableSSS) {
		glm::vec4 lightProj = _sceneData.viewProj * glm::vec4(lightDir, 0.0f);

		_dispatchListSSS = buildDispatchList(
			lightProj,
			glm::vec2(_sceneData.viewportSize.x, _sceneData.viewportSize.y)
		);
	}

	// Cascaded shadow map updates
	if (debug.enableShadows) {
		updateShadowCSM(lightDir);
	}

	// Now the temporal should be known if this frame is safe
	_sceneData.temporal.y = _isTemporalInvalid ? 0u : 1u;

	createSceneBuffer(frameCtx, allocator);

	// Vulkan requires a buffer created once its defined in used shader, even if that buffer isn't actually used.
	frameCtx.shadowCSM_UBO = BufferUtils::createUniformBuffer(_shadowCSM, allocator);
	frameCtx.cpuDeletion.push_function([&, allocator]() mutable {
		BufferUtils::destroyAllocatedBuffer(frameCtx.shadowCSM_UBO, allocator);
	});

	auto& meshes = gpuResources.getResgisteredMeshes();
	const bool& gpuAccelPath = Engine::getProfiler().isGPUAccelOn();
	// When a update from gpu to cpu path occurs the bvh structures need to be updated.
	// For simplicity I'm just gonna clear the structures and rebuild it.
	if (_currentPath == RenderPath::GPU && !gpuAccelPath) {
		_visState.cleanup();

		frameCtx.visSyncResult = Visibility::syncFromGlobalInstances(
			_visState,
			_globalInstances,
			_loadedScenes,
			meshes.meshData,
			_globalTransforms);
	}
	else {
		frameCtx.visSyncResult = Visibility::syncFromGlobalInstances(
			_visState,
			_globalInstances,
			_loadedScenes,
			meshes.meshData,
			_globalTransforms);
	}

	if (gpuAccelPath) {
		updateDrawDataGPUPath(frameCtx, gpuResources);
	}
	else {
		updateDrawDataCPUPath(frameCtx, gpuResources, debug);
	}
}

void RenderScene::updateDrawDataCPUPath(
	FrameContext& frameCtx,
	GPUResources& gpuResources,
	const DebugToggles& debug)
{
	_currentPath = RenderPath::CPU;

	Visibility::applySyncResult(
		_visState,
		frameCtx.visSyncResult);

	// CPU CULLING
	Visibility::cullBVHCollect(
		_visState,
		_currentFrustum,
		frameCtx.visibleInstances,
		_visibleWorldAABBs);


	if (!frameCtx.visibleInstances.empty()) {
		frameCtx.visibleCount = static_cast<uint32_t>(frameCtx.visibleInstances.size());

		//// Assign all unique instanceIDs to visible instances, enables map back to all worldaabbs.
		//uint32_t mainVisibleSetID = 0;
		//for (auto& inst : frameCtx.visibleInstances) {
		//	inst.instanceID = mainVisibleSetID++;
		//}

		if (debug.enableShadows) {
			frameCtx.shadowCasterInstances.reserve(std::max(1024u, frameCtx.visibleCount * 2u));

			for (uint32_t cascadeIndex = 0; cascadeIndex < MAX_SHADOW_CASCADES; ++cascadeIndex) {

				// First 3 cascades can get foilage shadows
				const bool allowAlphaMasked = (cascadeIndex != MAX_SHADOW_CASCADES - 1u);

				PassRange& cascadeRange = frameCtx.shadowCastersRanges[cascadeIndex];
				cascadeRange.firstInstance =
					static_cast<uint32_t>(frameCtx.shadowCasterInstances.size());

				Visibility::cullBVHCollectShadowCasters(
					_visState,
					_cascadeFrustums[cascadeIndex],
					frameCtx.shadowCasterInstances,
					gpuResources.getMaterialFlagsByID(),
					allowAlphaMasked
				);

				cascadeRange.visibleCount =
					static_cast<uint32_t>(frameCtx.shadowCasterInstances.size()) - cascadeRange.firstInstance;

				ASSERT(cascadeRange.firstInstance + cascadeRange.visibleCount <= frameCtx.shadowCasterInstances.size());
			}
		}

		if (LightingSystem::_mainFlashLight.isFlashLightOn()) {
			frameCtx.shadowCasterInstances.reserve(frameCtx.shadowCasterInstances.size() + frameCtx.visibleCount);

			frameCtx.flashLightShadowRange.firstInstance =
				static_cast<uint32_t>(frameCtx.shadowCasterInstances.size());

			Visibility::cullBVHCollectShadowCasters(
				_visState,
				LightingSystem::_mainFlashLight.frustum,
				frameCtx.shadowCasterInstances,
				gpuResources.getMaterialFlagsByID(),
				false
			);

			frameCtx.flashLightShadowRange.visibleCount =
				static_cast<uint32_t>(frameCtx.shadowCasterInstances.size()) - frameCtx.flashLightShadowRange.firstInstance;

			ASSERT(frameCtx.flashLightShadowRange.firstInstance +
				frameCtx.flashLightShadowRange.visibleCount <= frameCtx.shadowCasterInstances.size());
		}

		//// Same pattern as main set
		//uint32_t shadowCastersID = 0;
		//for (auto& inst : frameCtx.shadowCasterInstances) {
		//	inst.instanceID = shadowCastersID++;
		//}

		auto& meshes = gpuResources.getResgisteredMeshes();
		DrawPreparation::buildAndSortIndirectDraws(
			frameCtx,
			meshes.meshData,
			meshes.meshLODs,
			_visibleWorldAABBs,
			_sceneData.cameraPos,
			_curCamProjUnjittered,
			debug);

		DrawPreparation::uploadGPUBuffersForFrame(
			frameCtx,
			gpuResources,
			_globalTransforms,
			LightingSystem::_globalLightList,
			Backend::getTransferQueue(),
			bool(_sceneData.temporal.y),
			Engine::getProfiler().isGPUAccelOn());
	}
}

// The actual culling and build pass occurs inside the Renderer.cpp, this just handles the transfer queue updates
void RenderScene::updateDrawDataGPUPath(FrameContext& frameCtx, GPUResources& gpuResources)
{
	_currentPath = RenderPath::GPU;

	DrawPreparation::uploadGPUBuffersForFrame(
		frameCtx,
		gpuResources,
		_globalTransforms,
		LightingSystem::_globalLightList,
		Backend::getTransferQueue(),
		bool(_sceneData.temporal.y),
		Engine::getProfiler().isGPUAccelOn());
}

void RenderScene::cleanScene() {
	_loadedScenes.clear();
	_visState.cleanup();
	_globalTransforms.clear();
	_initializeTransformCopy = true;
}
