#pragma once

#include "ResourceTypes.h"

#include "renderer/frame/FrameResources.h"
#include "Bounds.h"

namespace RD = RendererDefinitions;


struct FlashLight final : public LocalLight
{
public:
	Frustum frustum;
	glm::mat4 viewProj;
	bool IsFlashLightActive() const { return flashLightEnabled || flashLightFlagsChanged; }
	bool IsFlashLightOn() const { return flashLightEnabled; }
	bool AreFlagsToggled() const { return flashLightFlagsChanged; }
	constexpr void ToggleFlashLight() noexcept
	{
		flashLightEnabled = !flashLightEnabled;

		flashLightFlagsChanged = true;
		if (flashLightEnabled)
		{
			flags |= RD::LIGHT_FLAG_FLASHLIGHT;
			flags &= ~RD::LIGHT_FLAG_FLASHLIGHT_OFF;
			flags |= RD::LIGHT_FLAG_CASTS_SPOT_SHADOW;
		}
		else
		{
			flags &= ~RD::LIGHT_FLAG_FLASHLIGHT;
			flags |= RD::LIGHT_FLAG_FLASHLIGHT_OFF;
			flags &= ~RD::LIGHT_FLAG_CASTS_SPOT_SHADOW;
		}
	}

	constexpr void InitFlags() noexcept
	{
		flags &= ~RD::LIGHT_FLAG_FLASHLIGHT;
		flags |= RD::LIGHT_FLAG_FLASHLIGHT_OFF;
		flags &= ~RD::LIGHT_FLAG_CASTS_SPOT_SHADOW;
	}

	void Init()
	{

	}

	// position and direction based off camera
	bool UpdateFlashLight(
		std::vector<LocalLight>& globalLightList,
		const uint32_t shadowMapID,
		const uint32_t cookieTexID,
		const glm::vec3& pos,
		const glm::vec3& dir,
		const float dt,
		const glm::vec2 mouseDelta,
		const glm::vec3 camForward);

private:
	glm::vec3 smoothedDir{0.0f, 0.0f, -1.0f};
	glm::vec3 smoothedPos{0.0f};

	glm::vec3 dirVelocity{0.0f}; // for spring
	glm::vec3 posVelocity{0.0f};

	float lagStrength = 40.0f;   // responsiveness
	float swayStrength = 0.025f; // camera motion

	bool flashLightFlagsChanged = false;
	bool flashLightEnabled = false;
};

namespace LightingSystem
{

	inline struct alignas(16) FlashLightSettings
	{
		float offsetRight = 0.07f;
		float offsetDown = -0.12f;
		float offsetFwd = 0.07f;
		float fovYScale = 1.5f;

		float nearProj = 0.1f;
		float farProjScale = 2.0f;
		float shadowBias = 0.0001f;
		float radiusTexels = 1.0f;

		float intensity = 30.0f;
		float radius = 20.0f;
		float outerDeg = 38.0f;
		float innerDeg = 22.0f;
	} _flashlightSettings{};

	ClusterBufferSizes ComputeClusterBufferSizes(
		uint32_t screenWidth,
		uint32_t screenHeight);

	inline struct LightIDTable {
		std::vector<uint32_t> activeLightIDs;

		std::vector<uint32_t> idToIndex;
		std::vector<uint32_t> newCopiedIDs;
		std::vector<uint32_t> cleanupIDs;
		uint32_t newIDCount = 0u;

		std::vector<uint32_t> freeIDs;
		std::vector<uint8_t> alive;


		std::vector<uint32_t> highlightedIDs;

		void Clear()
		{
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

	inline struct alignas(16) ClusteredData
	{
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

	const uint32_t& GetActiveLightCount();

	extern std::vector<LocalLight> _globalLightList;

	void SetTargetActiveLightCount(uint32_t targetCount);
	bool UpdateLightList();
	bool UpdateDynamicLightsOrbit(float deltaTime);

	extern FlashLight _mainFlashLight;

	void Init(GPUResources& resources);
	void Cleanup();
}
