#pragma once

#include "Core.h"
#include "renderer/RendererDefinitions.h"

namespace RD = RendererDefinitions;

//inline constexpr size_t MAX_VISIBLE_LIGHT_ID_GPU_BYTES = RD::MAX_LIGHTS * sizeof(uint32_t);
inline constexpr uint32_t MAX_LIGHTS_PER_CLUSTER = 512u;
inline constexpr uint32_t MAX_VISIBLE_LIGHTS = RD::MAX_LIGHTS - RD::LIGHT_LIST_STATIC_COUNT;

// An instance basically = mesh
struct Instance
{
	uint32_t instanceID   = UINT32_MAX;  // Unique runtime tag for a renderables list *not current used
	uint32_t meshID       = UINT32_MAX;  // global meshBuffer
	uint32_t materialID   = UINT32_MAX;  // global material buffer
	uint32_t transformID  = UINT32_MAX;  // global transform/prevTransform buffer
	uint32_t passType     = UINT32_MAX;  // opaque/transparent material pass
};

enum class LightType
{
	Directional,
	Point,
	Spot
};

struct LocalLight
{
	LightType type = LightType::Point;

	glm::vec3 position = glm::vec3(0.0f);
	float radius = 1.0f;

	glm::vec3 color = glm::vec3(1.0f);
	float intensity = 1.0f;

	glm::vec3 direction = { 0.0f, -1.0f, 0.0f }; // for spot
	float innerCos = 0.9f;

	float outerCos = 0.8f;
	uint32_t flags = 0;
};

struct ClusterTileSliceRanges
{
	uint32_t tileSizeX = 32;
	uint32_t tileSizeY = 32;
	uint32_t zSlices   = 24;
};

struct Cmaa2BufferSizes
{
	uint32_t deferredItemsCapacity = 0;
	uint32_t quadCountX = 0;
	uint32_t quadCountY = 0;
	uint32_t quadCount = 0;
	uint32_t pixelCount = 0;

	size_t controlBytes = 0;
	size_t shapeCandidatesBytes = 0;
	size_t deferredLocationsBytes = 0;
	size_t deferredHeadsBytes = 0;
	size_t deferredItemsBytes = 0;

	void UpdateCmaa2BufferSizes(const uint32_t extentWidth, const uint32_t extentHeight);
};

struct ClusterBufferSizes
{
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

	void UpdateClusterBufferSizes(
		uint32_t screenWidth,
		uint32_t screenHeight,
		uint32_t tileSizeX,
		uint32_t tileSizeY,
		uint32_t zSlices);
};

// === RENDER PASS PUSH CONSTANTS ===

struct alignas(16) SSAOPush {
	glm::vec2 tanHalfFov{0.0f};
	float effectRadius = 0.5f;
	float effectFalloffRange = 0.6f;

	glm::vec2 ndcToViewAdd{ 0.0f };
	glm::vec2 ndcToViewMul{ 0.0f };

	glm::vec2 ndcToViewMul_x_PixelSize{ 0.0f };

	float depthLinearizeMult = 0.0f;
	float depthLinearizeAdd = 0.0f;

	// Bilateral blur
	float sharpness = 2.0;
	float radius = 4.0;
	glm::vec2 blurDirection{ 0.0f };

	// For temporal noise
	uint32_t noiseIndex = 0u; // FrameIndex % 64u
	uint32_t hilbertLutID = UINT32_MAX;

	// Denoise
	float denoiseBlurBeta = 1.2f;
	uint32_t isFinalPass = 0u;
};

struct alignas(16) TAAPush {
	float minBlend = 0.05f;
	float maxBlend = 0.5f;
	float depthDisocclusionScale = 200.0f;
	float pad0;
};

struct alignas(16) VolumetricPush {
	float density = 0.002f;
	float scatteringStrength = 15.0f;
	float extinction = 0.08f;
	float heightFalloff = 0.05f;

	float maxDistance = 200.0f;
	float jitterStrength = 0.8f;
	float asymmetryFactor = 0.9f;
	float minTransmittance = 0.9f;

	int beamPower = 2;
	float blurRadius = 4.0f;
	float blurDepthSigma = 0.015f;
	float blurWeightSigma = 1.6f;

	glm::vec2 blurDirection{ 0.0f };
	glm::vec2 pad0{ 0.0f };
};

struct alignas(16) LensFlarePush {
	// Quarter res
	glm::vec2 outputRes{ 0.0f };
	glm::vec2 invOutputRes{ 0.0f };

	glm::vec2 sunUv{ 0.5f, 0.5f };
	float sunVisible = 1.0f;
	uint32_t rainbowLUTIndex = 0u;

	// Bright-pass params (FlareBright)
	float brightThreshold = 15.0f;
	float brightKnee = 7.5f; // 0.5 * threshold
	float brightIntensity = 1.0f;
	float pad0{ 0.0f };

	// Ring params (FlareGen)
	float ringInnerRadius = 0.1f;
	float ringOuterRadius = 0.20f;
	float chromaStrength = 1.0f;
	float pad1{ 0.0f };

	// Streak params (FlareGen)
	float streakStrength = 0.2f;
	float streakWidth = 0.01f;   // UV units
	float streakLength = 0.2f;   // UV units
	float pad2{ 0.0f };

	// Hi-Z occlusion params (FlareGen)
	float occlusionRadiusPixels = 0.01f;  // full-res pixels
	float occlusionDepthBias = 0.01f;     // linear depth bias
	float occlusionFade = 8.0f;           // higher = harder fade
	float pad3{ 0.0f };
};

// Active IDs map 1:1 with worldaabbs
struct alignas(16) VisibilityPush {
	uint64_t activeIDsBufferAddr;
	uint64_t worldAABBsBufferAddr;
	uint32_t activeCount;
	uint32_t hizEnabled;
	uint32_t pad0;
	uint32_t pad1;
};

// Screen space contact shadows usage
struct alignas(16) SSSPush {
	glm::vec4 lightCoords{0.0f};

	glm::ivec2 waveOffsets{0};
	glm::vec2 invDepthSize{0.0f};

	float surfaceThickness = 0.01f;
	float bilinearThreshold = 0.02f;
	float shadowContrast = 4.0f;
	float pad0;
};

struct alignas(16) ForwardPush {
	uint32_t activeLightCount;
	float oitDepthScale = 400.0f;
	uint32_t flashlightShadowMapID = UINT32_MAX;
	uint32_t flashlightCookieTexID = UINT32_MAX;
	glm::mat4 flashlightVP;
};

struct alignas(16) CMAA2Push {
	uint32_t halfWidth;
	uint32_t maxShapeCandidates;
	uint32_t maxDeferredItems;
	uint32_t maxDeferredLocations;
	// x: symmetry correction offset, y: dampening effect, z: simple blurriness
	glm::vec4 params = glm::vec4(0.22f, 0.15f, 0.1f, 0.0f);
};

struct alignas(16) BindlessAccessPush {
	uint32_t id0 = UINT32_MAX;
	uint32_t id1 = UINT32_MAX;
	uint32_t id2 = UINT32_MAX;
	uint32_t id3 = UINT32_MAX;
};

struct ToneMappingSettings {
	float cameraExposure = 0.18f;
	float maxLuminance = 0.0f;
	float midLuminance = 0.0f;
	float minLuminance = 0.0f;
};
