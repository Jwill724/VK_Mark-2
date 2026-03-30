#pragma once

#include "core/ResourceManager.h"
#include "renderer/gpu/Descriptor.h"

enum class LightType : uint32_t {
	Point = 0,
	Spot = 1
};

struct LocalLight {
	LightType type = LightType::Point;

	glm::vec3 position = glm::vec3(0.0f);
	float radius = 1.0f;

	glm::vec3 color = glm::vec3(1.0f);
	float intensity = 1.0f;

	glm::vec3 direction = { 0.0f, -1.0f, 0.0f }; // for spot
	float innerCos = 0.9f;

	float outerCos = 0.8f;
	uint32_t flags = 0;
	uint32_t shadowMapID = UINT32_MAX;
	uint32_t cookieTexID = UINT32_MAX;
};

struct FlashLight {
public:
	Frustum frustum;
	LocalLight spotLight;
	glm::mat4 viewProj;
	bool isFlashLightActive() const { return flashLightEnabled || flashLightFlagsChanged; }
	bool isFlashLightOn() const { return flashLightEnabled; }
	bool areFlagsToggled() const { return flashLightFlagsChanged; }
	inline constexpr void toggleFlashLight() {
		flashLightEnabled = !flashLightEnabled;

		flashLightFlagsChanged = true;
		if (flashLightEnabled) {
			spotLight.flags |= LIGHT_FLAG_FLASHLIGHT;
			spotLight.flags &= ~LIGHT_FLAG_FLASHLIGHT_OFF;
			spotLight.flags |= LIGHT_FLAG_CASTS_SPOT_SHADOW;
		}
		else {
			spotLight.flags &= ~LIGHT_FLAG_FLASHLIGHT;
			spotLight.flags |= LIGHT_FLAG_FLASHLIGHT_OFF;
			spotLight.flags &= ~LIGHT_FLAG_CASTS_SPOT_SHADOW;
		}
	}

	inline constexpr void initFlags() {
		spotLight.flags &= ~LIGHT_FLAG_FLASHLIGHT;
		spotLight.flags |= LIGHT_FLAG_FLASHLIGHT_OFF;
		spotLight.flags &= ~LIGHT_FLAG_CASTS_SPOT_SHADOW;
	}

	// position and direction based off camera
	bool updateFlashLight(
		std::vector<LocalLight>& globalLightList,
		const uint32_t shadowMapID,
		const uint32_t cookieTexID,
		const glm::vec3& pos,
		const glm::vec3& dir,
		const float dt,
		const glm::vec2 mouseDelta,
		const glm::vec3 camForward);

	glm::vec3 smoothedDir{0.0f, 0.0f, -1.0f};
	glm::vec3 smoothedPos{0.0f};

	glm::vec3 dirVelocity{0.0f}; // for spring
	glm::vec3 posVelocity{0.0f};

	float lagStrength = 40.0f;   // responsiveness
	float swayStrength = 0.025f; // camera motion

private:
	bool flashLightFlagsChanged = false;
	bool flashLightEnabled = false;
};

//struct LightHandle {
//	uint32_t index = 0u;
//	uint32_t generation = 0u;
//};

constexpr size_t MAX_GPU_LIGHTS_SIZE_BYTES = MAX_LIGHTS * sizeof(LocalLight);
constexpr size_t MAX_GPU_VISIBLE_LIGHT_ID_SIZE_BYTES = MAX_LIGHTS * sizeof(uint32_t);
//constexpr uint32_t MAX_LIGHTS_PER_CLUSTER = 256u;
//constexpr uint32_t MAX_LIGHTS_PER_CLUSTER = 128u;
constexpr uint32_t MAX_LIGHTS_PER_CLUSTER = 512u;
constexpr uint32_t MAX_VISIBLE_LIGHTS = MAX_LIGHTS - LIGHT_LIST_STATIC_COUNT;

struct ClusterBufferSizes {
	// Derived counts
	uint32_t tileCountX = 0;
	uint32_t tileCountY = 0;
	uint32_t tileCount = 0;
	uint32_t clusterCount = 0;

	size_t clusterCountsBytes = 0;
	size_t clusterOffsetsBytes = 0;
	size_t clusterCursorsBytes = 0;
	size_t clusterLightIDsBytes = 0;

	size_t clusterTileSliceRangesBytes = 0;
	size_t clusterScanScratchBytes = 0;

	//size_t clusterDebugStatsBytes = 0;
};

namespace LightingSystem {
	inline struct ClusteredSettings {
		uint32_t tileSizeX = 32;
		uint32_t tileSizeY = 32;
		uint32_t zSlices = 24;
	} _clusteredSettings{};

	inline struct alignas(16) FlashLightSettings {
		float offsetRight = 0.07f;
		float offsetDown = -0.12f;
		float offsetFwd = 0.07f;
		float fovYScale = 1.5f;

		float nearProj = 0.1f;
		float farProjScale = 2.0f;
		float shadowBias = 0.0001f;
		float radiusTexels = 1.0f;

		float intensity = 20.0f;
		float radius = 20.0f;
		float outerDeg = 38.0f;
		float innerDeg = 22.0f;
	} _flashLightSettings{};

	ClusterBufferSizes computeClusterBufferSizes(
		uint32_t screenWidth,
		uint32_t screenHeight,
		AllocatedBuffer& clusteredUBO,
		const VmaAllocator alloc);

	inline struct LightIDTable {
		std::vector<uint32_t> activeLightIDs;

		std::vector<uint32_t> idToIndex;
		std::vector<uint32_t> newCopiedIDs;
		std::vector<uint32_t> cleanupIDs;
		uint32_t newIDCount = 0u;

		std::vector<uint32_t> freeIDs;
		std::vector<uint8_t> alive;


		std::vector<uint32_t> highlightedIDs;

		void clear() {
			activeLightIDs.clear();
			newCopiedIDs.clear();
			cleanupIDs.clear();
			idToIndex.clear();
			freeIDs.clear();
			alive.clear();
			highlightedIDs.clear();

			newIDCount = 0u;
		}
	} _lightIDTable;

	inline struct alignas(16) ClusteredData {
		uint32_t tileSizeX = 0;
		uint32_t tileSizeY = 0;
		uint32_t zSlices = 0;
		uint32_t maxLightsPerCluster = MAX_LIGHTS_PER_CLUSTER;

		uint32_t tileCountX = 0;
		uint32_t tileCountY = 0;
		uint32_t clusterCount = 0;
		uint32_t maxVisibleLights = MAX_VISIBLE_LIGHTS;

		glm::vec4 pad0[6] = { glm::vec4(0.0f) };
	} _clusteredData{};

	extern bool _dynamicLightsEnabled;

	const uint32_t& getActiveLightCount();

	extern std::vector<LocalLight> _globalLightList;

	void setTargetActiveLightCount(uint32_t targetCount);
	bool updateLightList();
	bool updateDynamicLightsOrbit(float deltaTime);

	extern FlashLight _mainFlashLight;

	void init(GPUResources& resources);
	void cleanup();
}
