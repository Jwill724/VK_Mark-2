#include "pch.h"

#include "LightingSystem.h"
#include "utils/BufferUtils.h"

namespace LightingSystem {
	FlashLight _mainFlashLight;
	bool _dynamicLightsEnabled = false;

	std::vector<LocalLight> _globalLightList;
	uint32_t _activeLightCount = 0u;
	const uint32_t& getActiveLightCount() { return _activeLightCount; }

	void createClusteredUBO(AllocatedBuffer& clusteredUBO, const VmaAllocator alloc);

	static uint32_t getDynamicLightBeginIndex() {
		return LIGHT_LIST_STATIC_COUNT;
	}

	static bool isDynamicLightID(uint32_t lightID) {
		return lightID >= LIGHT_LIST_STATIC_COUNT;
	}

	static bool isLightIDAlive(uint32_t lightID) {
		if (lightID >= _lightIDTable.alive.size()) return false;
		return _lightIDTable.alive[lightID] != 0;
	}

	static uint32_t allocateLightID() {
		if (!_lightIDTable.freeIDs.empty()) {
			uint32_t reusedID = _lightIDTable.freeIDs.back();
			_lightIDTable.freeIDs.pop_back();

			ASSERT(reusedID < _lightIDTable.alive.size());
			ASSERT(reusedID < _lightIDTable.idToIndex.size());

			_lightIDTable.alive[reusedID] = 1u;
			_lightIDTable.idToIndex[reusedID] = UINT32_MAX;
			return reusedID;
		}

		uint32_t newID = static_cast<uint32_t>(_lightIDTable.alive.size());

		_lightIDTable.alive.push_back(1u);
		_lightIDTable.idToIndex.push_back(UINT32_MAX);

		return newID;
	}

	static void activateLight(LocalLight&& newLight, uint32_t lightID) {
		ASSERT(isDynamicLightID(lightID));
		ASSERT(lightID < _lightIDTable.idToIndex.size());
		ASSERT(_lightIDTable.idToIndex[lightID] == UINT32_MAX);

		uint32_t denseIndex = static_cast<uint32_t>(_globalLightList.size());

		_globalLightList.push_back(std::move(newLight));
		_lightIDTable.activeLightIDs.push_back(lightID);
		_lightIDTable.idToIndex[lightID] = denseIndex;
	}

	static uint32_t findActiveLightIDIndex(uint32_t lightID) {
		for (uint32_t activeIndex = 0; activeIndex < static_cast<uint32_t>(_lightIDTable.activeLightIDs.size()); ++activeIndex) {
			if (_lightIDTable.activeLightIDs[activeIndex] == lightID) {
				return activeIndex;
			}
		}

		return UINT32_MAX;
	}

	static LocalLight* getDynamicLightByID(uint32_t lightID) {
		if (!isDynamicLightID(lightID)) return nullptr;
		if (!isLightIDAlive(lightID)) return nullptr;

		uint32_t denseIndex = _lightIDTable.idToIndex[lightID];
		if (denseIndex == UINT32_MAX) return nullptr;
		if (denseIndex >= _globalLightList.size()) return nullptr;

		return &_globalLightList[denseIndex];
	}

	void createDefaultLight() {
		uint32_t newID = allocateLightID();

		LocalLight defaultLight{};
		defaultLight.type = LightType::Point;
		defaultLight.color = glm::vec3(1.0f);
		defaultLight.position = glm::vec3(0.0f);
		defaultLight.radius = 1.5f;
		defaultLight.intensity = 4.0f;
		defaultLight.shadowMapID = UINT32_MAX;
		defaultLight.cookieTexID = UINT32_MAX;

		activateLight(std::move(defaultLight), newID);
	}

	void createRandomLight() {
		uint32_t newID = allocateLightID();

		LocalLight randomLight{};

		const float rand01 = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
		const float rand02 = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
		const float rand03 = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
		const float rand04 = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
		const float rand06 = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
		const float rand07 = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
		const float rand08 = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);

		const float posRange = 4.0f;
		randomLight.position = glm::vec3(
			(rand01 * 2.0f - 1.0f) * (posRange * 2.5f),
			(rand02 * 2.0f - 1.0f) * (posRange * 1.5f),
			(rand03 * 2.0f - 1.0f) * (posRange * 1.5f)
		);

		randomLight.type = LightType::Point;
		randomLight.radius = 1.0f;
		randomLight.intensity = 4.0f;

		glm::vec3 colorA = glm::vec3(rand06, rand07, rand08);
		const float minColor = 0.1f;
		const float maxColor = 1.0f;
		randomLight.color = glm::clamp(colorA, glm::vec3(minColor), glm::vec3(maxColor));

		randomLight.shadowMapID = UINT32_MAX;
		randomLight.cookieTexID = UINT32_MAX;

		activateLight(std::move(randomLight), newID);
	}

	void destroyLight(uint32_t lightID) {
		if (!isDynamicLightID(lightID)) {
			fmt::print("[LightingSystem] Refusing to destroy static light id={}\n", lightID);
			return;
		}

		if (!isLightIDAlive(lightID)) return;

		ASSERT(lightID < _lightIDTable.idToIndex.size());

		uint32_t removedDenseIndex = _lightIDTable.idToIndex[lightID];
		if (removedDenseIndex == UINT32_MAX) return;

		ASSERT(removedDenseIndex >= LIGHT_LIST_STATIC_COUNT);
		ASSERT(removedDenseIndex < _globalLightList.size());

		uint32_t lastDenseIndex = static_cast<uint32_t>(_globalLightList.size() - 1u);

		uint32_t removedActiveIndex = findActiveLightIDIndex(lightID);
		ASSERT(removedActiveIndex != UINT32_MAX);

		if (removedDenseIndex != lastDenseIndex) {
			LocalLight& movedLight = _globalLightList[lastDenseIndex];

			uint32_t movedLightID = UINT32_MAX;
			for (uint32_t activeID : _lightIDTable.activeLightIDs) {
				if (_lightIDTable.idToIndex[activeID] == lastDenseIndex) {
					movedLightID = activeID;
					break;
				}
			}

			ASSERT(movedLightID != UINT32_MAX);

			_globalLightList[removedDenseIndex] = std::move(movedLight);
			_lightIDTable.idToIndex[movedLightID] = removedDenseIndex;
		}

		_globalLightList.pop_back();

		uint32_t lastActiveIndex = static_cast<uint32_t>(_lightIDTable.activeLightIDs.size() - 1u);
		if (removedActiveIndex != lastActiveIndex) {
			_lightIDTable.activeLightIDs[removedActiveIndex] = _lightIDTable.activeLightIDs[lastActiveIndex];
		}
		_lightIDTable.activeLightIDs.pop_back();

		_lightIDTable.idToIndex[lightID] = UINT32_MAX;
		_lightIDTable.alive[lightID] = 0u;
		_lightIDTable.freeIDs.push_back(lightID);
	}
}

void LightingSystem::createClusteredUBO(AllocatedBuffer& clusteredUBO, const VmaAllocator alloc) {
	if (clusteredUBO.buffer != VK_NULL_HANDLE) {
		BufferUtils::destroyAllocatedBuffer(clusteredUBO, alloc);
	}
	clusteredUBO = BufferUtils::createUniformBuffer(_clusteredData, alloc);
}


ClusterBufferSizes LightingSystem::computeClusterBufferSizes(
	uint32_t screenWidth,
	uint32_t screenHeight,
	AllocatedBuffer& clusteredUBO,
	const VmaAllocator alloc)
{
	ClusterBufferSizes sizes{};
	sizes.tileCountX = (screenWidth + _clusteredSettings.tileSizeX - 1u) / _clusteredSettings.tileSizeX;
	sizes.tileCountY = (screenHeight + _clusteredSettings.tileSizeY - 1u) / _clusteredSettings.tileSizeY;
	sizes.tileCount = sizes.tileCountX * sizes.tileCountY;
	sizes.clusterCount = sizes.tileCount * _clusteredSettings.zSlices;

	_clusteredData.tileCountX = sizes.tileCountX;
	_clusteredData.tileSizeX = _clusteredSettings.tileSizeX;
	_clusteredData.tileCountY = sizes.tileCountY;
	_clusteredData.tileSizeY = _clusteredSettings.tileSizeY;
	_clusteredData.zSlices = _clusteredSettings.zSlices;
	_clusteredData.clusterCount = sizes.clusterCount;

	createClusteredUBO(clusteredUBO, alloc);

	// Packed clustered buffers (per-frame)
	sizes.clusterCountsBytes = static_cast<size_t>(sizes.clusterCount) * sizeof(uint32_t);
	sizes.clusterOffsetsBytes = static_cast<size_t>(sizes.clusterCount) * sizeof(uint32_t);
	sizes.clusterCursorsBytes = static_cast<size_t>(sizes.clusterCount) * sizeof(uint32_t);

	// Packed flat list of light indices
	sizes.clusterLightIDsBytes =
		static_cast<size_t>(sizes.clusterCount) *
		static_cast<size_t>(MAX_LIGHTS_PER_CLUSTER) *
		sizeof(uint32_t);

	// Hi-Z tile slice ranges: uvec2 per tile (minSlice, maxSlice) = 8 bytes
	sizes.clusterTileSliceRangesBytes = static_cast<size_t>(sizes.tileCount) * 8u;

	// Scan scratch sizing
	const uint32_t elementsPerBlock = 256u;
	const uint32_t blockCount = (sizes.clusterCount + elementsPerBlock - 1u) / elementsPerBlock;

	sizes.clusterScanScratchBytes = 4u;

	//sizes.clusterDebugStatsBytes = 256u;

	sizes.clusterCountsBytes = BufferUtils::alignUp(sizes.clusterCountsBytes, 256u);
	sizes.clusterOffsetsBytes = BufferUtils::alignUp(sizes.clusterOffsetsBytes, 256u);
	sizes.clusterCursorsBytes = BufferUtils::alignUp(sizes.clusterCursorsBytes, 256u);
	sizes.clusterLightIDsBytes = BufferUtils::alignUp(sizes.clusterLightIDsBytes, 256u);

	sizes.clusterTileSliceRangesBytes = BufferUtils::alignUp(sizes.clusterTileSliceRangesBytes, 256u);
	sizes.clusterScanScratchBytes = BufferUtils::alignUp(sizes.clusterScanScratchBytes, 256u);

	return sizes;
}


void LightingSystem::init(GPUResources& resources) {
	const auto alloc = resources.getAllocator();
	auto& lightListStaging = resources.getLightListStagingBuffer();

	if (lightListStaging.buffer == VK_NULL_HANDLE) {
		lightListStaging = BufferUtils::createBuffer(
			MAX_GPU_LIGHTS_SIZE_BYTES,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
			alloc
		);
		ASSERT(lightListStaging.info.pMappedData);
	}

	_globalLightList.clear();
	_globalLightList.reserve(MAX_LIGHTS);
	_globalLightList.resize(LIGHT_LIST_STATIC_COUNT);

	_mainFlashLight.initFlags();
	_globalLightList[LIGHT_LIST_SLOT_FLASHLIGHT] = _mainFlashLight.spotLight;

	_lightIDTable.activeLightIDs.clear();
	_lightIDTable.freeIDs.clear();
	_lightIDTable.alive.clear();
	_lightIDTable.idToIndex.clear();

	_lightIDTable.highlightedIDs.clear();
	_lightIDTable.cleanupIDs.clear();
	_lightIDTable.newCopiedIDs.clear();

	_lightIDTable.activeLightIDs.reserve(MAX_LIGHTS);
	_lightIDTable.freeIDs.reserve(MAX_LIGHTS);
	_lightIDTable.alive.reserve(MAX_LIGHTS);
	_lightIDTable.idToIndex.reserve(MAX_LIGHTS);

	_lightIDTable.highlightedIDs.reserve(MAX_LIGHTS);
	_lightIDTable.cleanupIDs.reserve(MAX_LIGHTS);
	_lightIDTable.newCopiedIDs.reserve(MAX_LIGHTS);

	// Static slots are always "alive" and mapped to themselves.
	_lightIDTable.alive.resize(LIGHT_LIST_STATIC_COUNT, 1u);
	_lightIDTable.idToIndex.resize(LIGHT_LIST_STATIC_COUNT);

	for (uint32_t staticIndex = 0; staticIndex < LIGHT_LIST_STATIC_COUNT; ++staticIndex) {
		_lightIDTable.idToIndex[staticIndex] = staticIndex;
	}
}

void LightingSystem::setTargetActiveLightCount(uint32_t targetCount) {
	uint32_t currentCount = static_cast<uint32_t>(_lightIDTable.activeLightIDs.size());

	if (targetCount == currentCount) return;

	if (targetCount > currentCount) {
		_lightIDTable.newIDCount += (targetCount - currentCount);
		return;
	}

	uint32_t removeCount = currentCount - targetCount;

	for (uint32_t i = 0; i < removeCount; ++i) {
		uint32_t lastIndex = static_cast<uint32_t>(_lightIDTable.activeLightIDs.size() - 1u - i);
		uint32_t lightID = _lightIDTable.activeLightIDs[lastIndex];
		_lightIDTable.cleanupIDs.push_back(lightID);
	}
}

bool FlashLight::updateFlashLight(
	std::vector<LocalLight>& globalLightList,
	const uint32_t shadowMapID,
	const uint32_t cookieTexID,
	const glm::vec3& pos,
	const glm::vec3& dir,
	const float dt,
	const glm::vec2 mouseDelta,
	const glm::vec3 camForward)
{
	bool lightStateUpdated = false;

	// Static setup
	if (shadowMapID != spotLight.shadowMapID || cookieTexID != spotLight.cookieTexID) {
		spotLight.cookieTexID = cookieTexID;
		spotLight.shadowMapID = shadowMapID;
		spotLight.type = LightType::Spot;
		spotLight.intensity = LightingSystem::_flashLightSettings.intensity;
		spotLight.radius = LightingSystem::_flashLightSettings.radius;

		const float outerDeg = LightingSystem::_flashLightSettings.outerDeg;
		const float innerDeg = LightingSystem::_flashLightSettings.innerDeg;
		spotLight.outerCos = std::cos(glm::radians(outerDeg));
		spotLight.innerCos = std::cos(glm::radians(innerDeg));

		lightStateUpdated = true;
	}

	glm::vec3 flatForward = glm::normalize(glm::vec3(camForward.x, 0.0f, camForward.z));
	const glm::vec3 upWorld = glm::vec3(0.0f, 1.0f, 0.0f);

	// Handle edge case (looking straight up/down)
	if (glm::length(flatForward) < 0.001f) {
		flatForward = glm::vec3(0.0f, 0.0f, -1.0f);
	}

	glm::vec3 stableRight = glm::normalize(glm::cross(flatForward, upWorld));
	glm::vec3 stableUp = upWorld;

	// Reduce sway when looking up/down
	float verticalFactor = 1.0f - std::abs(camForward.y);
	verticalFactor = glm::smoothstep(0.0f, 1.0f, verticalFactor);

	glm::vec3 swayOffset =
		stableRight * (-mouseDelta.x * swayStrength) +
		stableUp    * (-mouseDelta.y * swayStrength);

	swayOffset *= verticalFactor;

	glm::vec3 targetDir = glm::normalize(camForward + swayOffset);
	glm::vec3 targetPos = pos;

	// smoothing (exponential)
	float response = 1.0f - std::exp(-lagStrength * dt);

	smoothedDir = glm::normalize(glm::mix(smoothedDir, targetDir, response));
	smoothedPos = glm::mix(smoothedPos, targetPos, response);

	if (spotLight.direction != smoothedDir || spotLight.position != smoothedPos) {
		spotLight.direction = smoothedDir;
		spotLight.position = smoothedPos;
		lightStateUpdated = true;
	}

	spotLight.intensity = LightingSystem::_flashLightSettings.intensity;

	// Matrix update
	if (lightStateUpdated) {
		glm::vec3 fwd = glm::normalize(spotLight.direction);
		glm::vec3 right = glm::normalize(glm::cross(fwd, upWorld));
		glm::vec3 up = glm::normalize(glm::cross(right, fwd));

		const float offsetRight = LightingSystem::_flashLightSettings.offsetRight;
		const float offsetDown = LightingSystem::_flashLightSettings.offsetDown;
		const float offsetFwd = LightingSystem::_flashLightSettings.offsetFwd;

		glm::vec3 lightPos =
			spotLight.position +
			right * offsetRight +
			up * offsetDown +
			fwd * offsetFwd;

		glm::mat4 view = glm::lookAt(
			lightPos,
			lightPos + fwd,
			up
		);

		const float fovY = LightingSystem::_flashLightSettings.fovYScale;

		glm::mat4 proj = glm::perspective(
			fovY,
			1.0f,
			0.1f,
			spotLight.radius
		);

		viewProj = proj * view;
		frustum = extractFrustum(viewProj);
	}
 
	// Push to global list
	const bool lightDirty = lightStateUpdated || flashLightFlagsChanged;
	if (lightDirty) {
		flashLightFlagsChanged = false;
		globalLightList[LIGHT_LIST_SLOT_FLASHLIGHT] = spotLight;
	}

	return lightDirty;
}

bool LightingSystem::updateLightList() {
	bool listChanged = false;

	for (uint32_t lightID : _lightIDTable.cleanupIDs) {
		destroyLight(lightID);
		listChanged = true;
	}
	_lightIDTable.cleanupIDs.clear();

	uint32_t activeCount = static_cast<uint32_t>(_lightIDTable.activeLightIDs.size());

	for (uint32_t sourceID : _lightIDTable.newCopiedIDs) {
		if (activeCount >= MAX_LIGHTS) {
			fmt::print("[LightingSystem::updateLightList] copy break: activeCount={} max={}\n",
				activeCount,
				MAX_LIGHTS
			);
			break;
		}

		if (!isLightIDAlive(sourceID)) {
			fmt::print("[LightingSystem::updateLightList] copy skip: sourceID={} not alive\n", sourceID);
			continue;
		}

		LocalLight* sourceLight = getDynamicLightByID(sourceID);
		if (sourceLight == nullptr) {
			fmt::print("[LightingSystem::updateLightList] copy skip: sourceID={} has no mapped dense light\n", sourceID);
			continue;
		}

		uint32_t newID = allocateLightID();

		fmt::print("[LightingSystem::updateLightList] copy: sourceID={} -> newID={} (activeBefore={})\n",
			sourceID,
			newID,
			activeCount
		);

		LocalLight copiedLight = *sourceLight;
		activateLight(std::move(copiedLight), newID);

		++activeCount;
		listChanged = true;
	}
	_lightIDTable.newCopiedIDs.clear();

	uint32_t createCount = _lightIDTable.newIDCount;

	while (createCount > 0 && activeCount < MAX_LIGHTS) {
		createRandomLight();
		--createCount;
		++activeCount;

		listChanged = true;
	}

	_lightIDTable.newIDCount = 0u;

	_activeLightCount = static_cast<uint32_t>(_lightIDTable.activeLightIDs.size());

	if (_mainFlashLight.isFlashLightOn()) {
		_activeLightCount++;
	}

	ASSERT(_activeLightCount <= MAX_LIGHTS);

	return listChanged;
}

static void rotateLightAroundOriginXZ(
	LocalLight& light,
	float angularSpeedRad,
	float deltaTime)
{
	const float angle = angularSpeedRad * deltaTime;

	const float cosA = std::cos(angle);
	const float sinA = std::sin(angle);

	glm::vec3 p = light.position;

	glm::vec3 rotated;
	rotated.x = p.x * cosA - p.z * sinA;
	rotated.z = p.x * sinA + p.z * cosA;
	rotated.y = p.y;

	light.position = rotated;
}

bool LightingSystem::updateDynamicLightsOrbit(float deltaTime) {
	if (_lightIDTable.activeLightIDs.empty()) return false;

	constexpr float baseSpeed = glm::radians(0.8f);

	uint32_t index = 0;
	for (uint32_t lightID : _lightIDTable.activeLightIDs) {
		LocalLight* light = getDynamicLightByID(lightID);
		if (light == nullptr) {
			++index;
			continue;
		}

		float speed = baseSpeed * (0.5f + 0.05f * index);

		rotateLightAroundOriginXZ(
			*light,
			speed,
			deltaTime
		);

		++index;
	}

	return true;
}

void LightingSystem::cleanup() {
	_lightIDTable.clear();
	_activeLightCount = 0u;
	_globalLightList.clear();
}
