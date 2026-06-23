#pragma once

#include <renderer/backend/VulkanForward.h>
#include "renderer/RendererDefinitions.h"
#include "Bounds.h"
#include "../scene/World.h"

// Note: Instance and LocalLight sizes for buffers are predefined in renderer/backend/memory/Budgets.h

namespace RD = RendererDefinitions;

inline constexpr uint32_t TIMESTAMP_PASS_COUNT       = static_cast<uint32_t>(RD::PASS_COUNT);
inline constexpr uint32_t PASS_TIMESTAMP_QUERY_COUNT = TIMESTAMP_PASS_COUNT * 2u;
inline constexpr uint32_t FRAME_BEGIN_QUERY          = PASS_TIMESTAMP_QUERY_COUNT;
inline constexpr uint32_t FRAME_END_QUERY            = PASS_TIMESTAMP_QUERY_COUNT + 1u;
inline constexpr uint32_t TIMESTAMP_QUERY_COUNT      = PASS_TIMESTAMP_QUERY_COUNT + 2u;

namespace RD = RendererDefinitions;

// An instance basically = mesh
struct Instance
{
	uint32_t meshID       = UINT32_MAX;  // global meshBuffer
	uint32_t materialID   = UINT32_MAX;  // global material buffer
	uint32_t transformID  = UINT32_MAX;  // global transform/prevTransform buffer
	uint32_t passType     = UINT32_MAX;  // opaque/transparent material pass
};

struct CoreSlab
{
	uint32_t first = 0u;
	uint32_t stride = 0u;
	uint32_t usedCopies = 0u;
};

struct BVHNode
{
	AABB box; // node bounds
	glm::vec3 extent;
	glm::vec3 origin;
	float sphereRadius;
	int left = -1; // child indices; -1 => leaf
	int right = -1;
	uint32_t first = 0; // start index into leafIndex[]
	uint16_t count = 0; // leaf count (0 for internal)
};

// Instances in VisibilityState go into one row per cullable unit,
// that can be drawm = mesh x copy.
// Built when copies change (multi-static slider), not per-frame.

struct VisibilityState
{
	std::vector<Instance> instances;    // per mesh X copy
	std::vector<AABB> worldAABBs;       // parallel to coreStatic
	std::vector<uint32_t> transformIDs; // parallel to coreStatic
	std::unordered_map<ModelID, CoreSlab> slabs;

	std::vector<uint32_t> active;    // live rows (indices into coreStatic)
	std::vector<uint32_t> leafIndex; // permutation used by BVH build
	std::vector<BVHNode> bvh;

	void Cleanup()
	{
		instances.clear();
		worldAABBs.clear();
		transformIDs.clear();
		slabs.clear();

		active.clear();
		leafIndex.clear();
		bvh.clear();
	}
};

enum class LightType
{
	//Directional,
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
		uint32_t tileSizeX = RD::CLUSTERS_TILE_SLICE_X,
		uint32_t tileSizeY = RD::CLUSTERS_TILE_SLICE_Y,
		uint32_t zSlices   = RD::CLUSTERS_TILE_SLICE_Z);
};

struct alignas(16) LightClustersData
{
	uint32_t tileSizeX = RD::CLUSTERS_TILE_SLICE_X;
	uint32_t tileSizeY = RD::CLUSTERS_TILE_SLICE_Y;
	uint32_t zSlices = RD::CLUSTERS_TILE_SLICE_Z;
	uint32_t maxLightsPerCluster = RD::MAX_LIGHTS_PER_CLUSTER;

	uint32_t tileCountX = 0;
	uint32_t tileCountY = 0;
	uint32_t clusterCount = 0;
	uint32_t maxVisibleLights = RD::MAX_VISIBLE_LIGHTS;

	glm::vec4 pad0[6] = { glm::vec4(0.0f) };
};

struct InstanceRange
{
	uint32_t firstInstance = 0;
	uint32_t visibleCount = 0;
};

// Indirect draw buffer can fit in lots of different draws
struct IndirectDrawRange
{
	uint32_t firstCommand = 0;   // first indirect command index in frameCtx.indirectDraws
	uint32_t commandCount = 0;   // number of subdraws inside of VkDrawIndexedIndirectCommand
};

struct ShadowDrawRange
{
	IndirectDrawRange drawRange{};
	InstanceRange instanceRange{};
};

class InstanceWriteScope
{
public:
	InstanceWriteScope(std::vector<Instance>& out)
		: m_instances(out)
	{
		m_range.firstInstance = static_cast<uint32_t>(out.size());
	}

	void Add(const Instance& inst)
	{
		m_instances.emplace_back(inst);
	}

	void End()
	{
		m_range.visibleCount =
			static_cast<uint32_t>(m_instances.size()) - m_range.firstInstance;
	}

	const InstanceRange& GetRange() const { return m_range; }

private:
	std::vector<Instance>& m_instances;
	InstanceRange m_range{};
};

class IndirectDrawScope
{
public:
	IndirectDrawScope(std::vector<VkDrawIndexedIndirectCommand>& out)
		: m_draws(out)
	{
		m_range.firstCommand = static_cast<uint32_t>(out.size());
	}

	void Add(const VkDrawIndexedIndirectCommand& cmd)
	{
		m_draws.emplace_back(cmd);
	}

	void End()
	{
		m_range.commandCount =
			static_cast<uint32_t>(m_draws.size()) - m_range.firstCommand;
	}

	const IndirectDrawRange& GetRange() const { return m_range; }

private:
	std::vector<VkDrawIndexedIndirectCommand>& m_draws;
	IndirectDrawRange m_range{};
};

struct DrawBuildOutput
{
	std::vector<Instance> visibleInstances{};
	std::vector<VkDrawIndexedIndirectCommand> indirectDraws{};

	InstanceRange opaqueInstances{};
	IndirectDrawRange opaqueDraws{};

	InstanceRange transparentInstances{};
	IndirectDrawRange transparentDraws{};

	std::array<ShadowDrawRange, RD::MAX_SHADOW_CASCADES> csm{};
	ShadowDrawRange flashlight{};
};

// Some chunk of a flat array that needs to go
struct DirtyRange
{
	uint32_t offset = 0;
	uint32_t count = 0;
};

// === Per-frame sync ===
// Compares current VirtualInstance.usedCopies/firstTransform to visState.slabs and decides:
//  - grow: appendSceneCopies + buildBVH()
//  - shrink: shrinkSceneCopiesLazy + buildBVH()
//  - relocate-only: rewriteSceneSlice + refitBVH()
//  - no change: do nothing
// Returns whether topology changed or a refit-only is needed, plus any transform upload ranges.
struct VisibilitySyncResult
{
	bool topologyChanged = false;   // grew/shrank -> call buildBVH()
	bool refitOnly = false;         // only transforms changed -> call refitBVH()
	std::vector<DirtyRange> dirtyTransformRanges; // for GPU uploads
};

// === RENDER PASS PUSH CONSTANTS ===


struct alignas(16) BindlessAccessPush
{
	uint32_t id0 = UINT32_MAX;
	uint32_t id1 = UINT32_MAX;
	uint32_t id2 = UINT32_MAX;
	uint32_t id3 = UINT32_MAX;
};

struct alignas(16) ForwardPush
{
	uint32_t activeLightCount = 0;

	uint32_t diffuseID = UINT32_MAX;
	uint32_t specularID = UINT32_MAX;
	uint32_t brdfID = UINT32_MAX;

	float oitDepthScale = 400.0f;
	uint32_t flashlightShadowMapID = UINT32_MAX;
	uint32_t flashlightCookieTexID = UINT32_MAX;
	uint32_t pad0;
	glm::mat4 flashlightVP;
};

struct alignas(16) SSAOPush
{
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

struct alignas(16) TAAPush
{
	float minBlend = 0.0125f;
	float maxBlend = 0.25f;
	float depthDisocclusionScale = 200.0f;
	float pad0;
};

struct alignas(16) VolumetricPush
{
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

struct alignas(16) LensFlarePush
{
	// Quarter res
	glm::vec2 outputRes{ 0.0f };
	glm::vec2 invOutputRes{ 0.0f };

	glm::vec2 sunUv{ 0.5f, 0.5f };
	float sunVisible = 1.0f;
	uint32_t rainbowLUTIndex = UINT32_MAX;

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
struct alignas(16) VisibilityPush
{
	uint64_t activeIDsBufferAddr;
	uint64_t worldAABBsBufferAddr;
	uint32_t activeCount;
	uint32_t hizEnabled;
	uint32_t pad0;
	uint32_t pad1;
};

// Screen space contact shadows usage
struct alignas(16) SSSPush
{
	glm::vec4 lightCoords{0.0f};

	glm::ivec2 waveOffsets{0};
	glm::vec2 invDepthSize{0.0f};

	float surfaceThickness = 0.01f;
	float bilinearThreshold = 0.02f;
	float shadowContrast = 4.0f;
	float pad0;
};

struct alignas(16) CMAA2Push
{
	uint32_t halfWidth;
	uint32_t maxShapeCandidates;
	uint32_t maxDeferredItems;
	uint32_t maxDeferredLocations;
	// x: symmetry correction offset, y: dampening effect, z: simple blurriness, w: indirect dispatch pass : 0 candidate / 1 deferred loc count
	glm::vec4 params = glm::vec4(0.22f, 0.15f, 0.1f, 0.0f);
};

struct alignas(16) SkyboxPush
{
	glm::mat4 invVp = glm::mat4(0.0f);
	uint32_t skyboxID = UINT32_MAX;
	uint32_t pad0[3];
};

struct alignas(16) LightCullingPush
{
	uint32_t activeLightCount;
	uint32_t pad0;
	uint32_t pad1;
	uint32_t pad2;
	glm::vec4 planes[6];
};

struct ToneMappingSettings
{
	float cameraExposure = 0.18f;
	float maxLuminance = 0.0f;
	float midLuminance = 0.0f;
	float minLuminance = 0.0f;
};

struct alignas(16) LumaExposurePush
{
	uint32_t totalLumaTiles = 0;
	float cameraExposure = 0.0;
	float adaptationSpeed = 0.0;
	float deltaTime = 0.0;
};
