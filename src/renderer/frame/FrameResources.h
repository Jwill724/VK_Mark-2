#pragma once

#include <renderer/backend/VulkanForward.h>
#include "renderer/RendererDefinitions.h"
#include "../scene/World.h"

// Note: Instance and LocalLight sizes for buffers are predefined in renderer/backend/memory/Budgets.h

namespace RD = RendererDefinitions;

inline constexpr uint32_t TIMESTAMP_PASS_COUNT       = static_cast<uint32_t>(RD::PASS_COUNT);
inline constexpr uint32_t PASS_TIMESTAMP_QUERY_COUNT = TIMESTAMP_PASS_COUNT * 2u;
inline constexpr uint32_t FRAME_BEGIN_QUERY          = PASS_TIMESTAMP_QUERY_COUNT;
inline constexpr uint32_t FRAME_END_QUERY            = PASS_TIMESTAMP_QUERY_COUNT + 1u;
inline constexpr uint32_t TIMESTAMP_QUERY_COUNT      = PASS_TIMESTAMP_QUERY_COUNT + 2u;

namespace RD = RendererDefinitions;

enum InstanceFlags : uint32_t
{
	PASS_OPAQUE      = 1 << 0,
	PASS_TRANSPARENT = 1 << 1,
	STATIC_OBJECT    = 1 << 2,
	DYNAMIC_OBJECT   = 1 << 3,
	CAST_CSM         = 1 << 4,
	CAST_FLASHLIGHT  = 1 << 5,
	RECEIVE_SHADOW   = 1 << 6,
	OCCLUDABLE       = 1 << 7,
	LOD_ENABLED      = 1 << 8,
	ALPHA_TESTED     = 1 << 9,
	DOUBLE_SIDED     = 1 << 10,
	GPU_SKINNED      = 1 << 11,
	ALWAYS_VISIBLE   = 1 << 12,
	IS_TREE          = 1 << 13,
	HAS_NORMALS      = 1 << 14,
	INSTANCE_ACTIVE  = 1 << 15,
	RT_VISIBLE       = 1 << 16,
	TRANSMISSIVE     = 1 << 17,
};

struct InstanceInput
{
	uint32_t meshID       = UINT32_MAX;
	uint32_t materialID   = UINT32_MAX;
	uint32_t transformID  = UINT32_MAX;
	uint32_t meshletVisibilityOffset = 0u;
	uint32_t lod0         = UINT32_MAX;
	uint32_t lod1         = UINT32_MAX;
	uint32_t lod2         = UINT32_MAX;
	uint32_t lod3         = UINT32_MAX;
	uint32_t shadowLod0   = UINT32_MAX;
	uint32_t shadowLod1   = UINT32_MAX;
	uint32_t shadowLod2   = UINT32_MAX;
	uint32_t rtMeshID     = UINT32_MAX;
	uint32_t flags        = UINT32_MAX;
};

struct DrawBin
{
	uint32_t meshID = UINT32_MAX;
	uint32_t materialID = UINT32_MAX;

	uint32_t instanceOffset = UINT32_MAX;
	uint32_t instanceCount = UINT32_MAX;
};

struct DebugDraw
{
	uint32_t type;

	uint32_t instanceID;

	uint32_t color;

	uint32_t flags;
};

struct GPUStats
{
	uint32_t visibleOpaque = 0;
	uint32_t visibleTransparent = 0;
	uint32_t visibleShadowCasters = 0;

	//uint32_t opaqueDrawCount = 0;
	//uint32_t transparentDrawCount = 0;
	//uint32_t shadowDrawCount = 0;

	uint32_t triangleCount = 0;  // Doesnt count shadows

	// --- mesh shader path ---
	uint32_t meshletsSubmitted = 0;
	uint32_t meshletsDrawnEarly = 0;
	uint32_t meshletsDrawnLate = 0;
	uint32_t meshletsCulledFrustum = 0;
	uint32_t meshletsCulledCone = 0;
	uint32_t meshletsCulledHiZ = 0;
	uint32_t meshletTriangles = 0;
};

struct CoreSlab
{
	uint32_t first = 0u;
	uint32_t stride = 0u;
	uint32_t usedCopies = 0u;
};

// Instances in InstanceState go into one row per cullable unit,
// that can be drawm = mesh x copy.
struct InstanceState
{
	std::vector<InstanceInput> gpuInputs; // per mesh X copy
	//std::vector<AABB> worldAABBs;        // parallel to coreStatic
	std::unordered_map<ModelID, CoreSlab> slabs;
	std::vector<uint32_t> active;    // live rows (indices into coreStatic)
	std::vector<uint32_t> rtRows;
	uint32_t rtInstanceCount = 0;
	void Cleanup() noexcept
	{
		gpuInputs.clear();
		active.clear();
		rtRows.clear();
		//worldAABBs.clear();
		slabs.clear();
		rtInstanceCount = 0;
	}
};

struct LocalLight
{
	glm::vec3 position = glm::vec3(0.0f);
	float radius = 1.0f;

	glm::vec3 color = glm::vec3(1.0f);
	float intensity = 1.0f;

	glm::vec3 direction = { 0.0f, -1.0f, 0.0f }; // for spot
	float innerCos = 0.9f;

	float outerCos = 0.8f;
	uint32_t flags = 0;

	float sourceRadius = 0.0f;
	float sourceLength = 0.0f;

	float changeRate = 0.0f;
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
	size_t tileTransparentNearBytes = 0;

	void UpdateClusterBufferSizes(
		uint32_t screenWidth,
		uint32_t screenHeight,
		uint32_t tileSizeX = RD::CLUSTERS_TILE_SLICE_X,
		uint32_t tileSizeY = RD::CLUSTERS_TILE_SLICE_Y,
		uint32_t zSlices   = RD::CLUSTERS_TILE_SLICE_Z);
};

struct RTRayListHeader
{
	uint32_t rayCount;
	uint32_t rayCapacity;
};

struct RTRayListLayout
{
	static constexpr uint32_t HEADER_BYTES = RD::RT_RAY_SLOT_COUNT * sizeof(uint32_t);

	uint32_t capacities[RD::RT_RAY_SLOT_COUNT]{};
	uint32_t bases[RD::RT_RAY_SLOT_COUNT]{};

	uint32_t totalBytes = 0;
	uint32_t halfWidth = 0;
	uint32_t halfHeight = 0;

	void Update(uint32_t screenWidth, uint32_t screenHeight);
};

struct SunBasis
{
	glm::vec3 direction{ 0.0f };
	glm::vec3 tangent{ 0.0f };
	glm::vec3 bitangent{ 0.0f };
};

inline SunBasis BuildSunBasis(const glm::vec3& sunDir)
{
	const glm::vec3 n = glm::normalize(sunDir);

	const float s = n.z >= 0.0f ? 1.0f : -1.0f;
	const float a = -1.0f / (s + n.z);
	const float b = n.x * n.y * a;

	SunBasis basis{};
	basis.direction = n;
	basis.tangent = glm::vec3(1.0f + s * n.x * n.x * a, s * b, -s * n.x);
	basis.bitangent = glm::vec3(b, s + n.y * n.y * a, -n.y);
	return basis;
}

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
	glm::vec2 halfTexel = glm::vec2(0.0f);
	uint32_t specularID = UINT32_MAX;
	uint32_t brdfID = UINT32_MAX;
	float oitDepthScale = 400.0f;
	float bounceFeedback = 0.6f;
	float giIntensity = 5.0f;
	uint32_t flashlightShadowMapID = UINT32_MAX;
	uint32_t flashlightCookieTexID = UINT32_MAX;
	float reflectRoughFade = 0.0f;
	float reflectRoughCutoff = 0.0f;

	float pad0;
};

struct alignas(16) SSGIPush
{
	float effectRadius = 10.0f;
	float effectFalloffRange = 0.6f;

	glm::vec2 ndcToViewMul_x_PixelSize{ 0.0f };

	float radiusMultiplier = 1.457;
	float sampleDistributionPower = 2.0f;

	// For temporal noise
	uint32_t noiseIndex = 0u; // FrameIndex % 64u
	uint32_t hilbertLutID = UINT32_MAX;

	// Denoise
	float denoiseBlurBeta = 1.0f;
	uint32_t isFinalPass = 0u;

	float upsampleDepthSigma = 256.0f;

	float giClampMax          = 8.0f;
	float giReprojTolerance   = 0.1f;
	float giTemporalAlpha     = 0.08f;
	float giFallbackStrength  = 0.4f;

	float pad0;
};

struct alignas(16) TAAPush
{
	float invDeltaTime = 0.0f;
	float clampGamma = 6.0f;
	float depthRejectScale = 5.0f;
	float motionSpeedScale = 0.0003f;

	float sigmaFloor = 0.02f;
	float shadingResponse = 2.0;
	float shadingRejectScale = 1.5f;
	float pad0;
};

struct alignas(16) CASPush
{
	float sharpness = 0.5f;
	float denoise = 1.0f;
	float hdrCompress = 0.0f;
	float pad0 = 0.0f;
};

struct alignas(16) VolumetricPush
{
	float density = 0.002f;
	float scatteringStrength = 5.0f;
	float extinction = 0.08f;
	float heightFalloff = 0.06f;

	float maxDistance = 100.0f;
	float jitterStrength = 0.9f;
	float asymmetryFactor = 0.5f;
	float minTransmittance = 0.9f;

	int beamPower = 4;
	float blurRadius = 4.0f;
	float blurDepthSigma = 0.5f;
	float blurWeightSigma = 1.6f;

	glm::vec2 blurDirection{ 0.0f };
	float historyWeight = 0.92f;
	float clipGamma = 1.25f;
};

struct alignas(16) LensFlarePush
{
	// Quarter res
	glm::vec2 outputRes{ 0.0f };
	glm::vec2 invOutputRes{ 0.0f };

	glm::vec2 sunUv{ 0.5f, 0.5f };
	float sunVisible = 1.0f;
	uint32_t rainbowLUTIndex = UINT32_MAX;

	// Bright pass
	float brightThreshold = 6.0f;      // display-space luminance, exposure divided in shader
	float brightKnee = 3.0f;
	float brightIntensity = 1.0f;
	float starburstIntensity = 1.2f;

	// Halo
	float ringInnerRadius = 0.2f;    // fraction of the SHORT screen axis
	float ringOuterRadius = 0.25f;
	float chromaStrength = 0.8f;
	float haloAnisotropy = 0.4f;

	// Anamorphic streak
	float streakStrength = 0.12f;
	float streakWidth = 0.012f;
	float streakLength = 0.22f;
	float starburstRotation = -2.0f;

	// Hi-Z occlusion
	float occlusionRadiusPixels = 5.0f;
	float occlusionDepthBias = 0.0f;
	float occlusionFade = 200.0f;      // world units, set from farClip * 0.02
	float sunJitterScale = 0.0f;

	// Starburst shape
	float starburstBlades = 6.0f;
	float starburstLength = 0.1f;
	float starburstWidth = 0.08f;     // ray angular half-width, radians
	float haloOpacity = 0.03f;

	float haloSqueeze = 1.55f;
	float haloAngleGain = 2.50f;
	float ghostStrength = 0.060f;
	float ghostSpacing = 1.00f;
};

// Screen space contact shadows usage
struct alignas(16) SSSPush
{
	glm::vec4 lightCoords{0.0f};

	glm::ivec2 waveOffsets{0};
	glm::vec2 invDepthSize{0.0f};

	float surfaceThickness = 0.005f;
	float bilinearThreshold = 0.1f;
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

struct alignas(16) BloomPush
{
	glm::vec2 srcTexelSize;
	glm::uvec2 dstRes;
	float filterRadius = 1.0f;
	uint32_t flags; // 0 = first downsample
	float bloomThreshold = 1.0f;
	float bloomKnee = 1.0f;
	float emissiveBoost = 1.5f;
	float pad0;
	float pad1;
	float pad2;
};

struct alignas(16) DownsamplePush
{
	glm::vec2 srcTexel = glm::vec2(0.0f);
	uint32_t applyKaris = 0;
	uint32_t pad0;
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

struct alignas(16) PrepassTaskPush
{
	uint32_t slot;     // VIS_SLOT_*
	uint32_t phase;    // 0 = phase 1, 1 = phase 2 (enables Hi-Z test)
	uint32_t pad0;
	uint32_t pad1;
};

struct alignas(16) DepthTaskPush
{
	glm::mat4 viewproj;
	glm::vec4 eye;             // xyz = eye pos (w=1) or light dir (w=0)
	uint32_t  slot;
	float  cullDistance = 0.0; // > 0 enables the range test; 0 disables (directional)
	uint32_t pad0[2];
};

struct alignas(16) TlasPush
{
	uint32_t instanceCount = 0;
	uint32_t pad0[3];
};

struct alignas(16) NRDPush
{
	glm::vec2 resSize{ 0.0f };
	glm::vec2 resTexel{ 0.0f };
	uint32_t writeMotion = 0;
	uint32_t pad0[3];
};

struct alignas(16) RTArgsPush
{
	uint32_t raySlot = 0u;
	uint32_t argsSlot = 0u;
	uint32_t groupSize = 64u;
	uint32_t rayCapacity = 0u;
};

struct alignas(16) RTShadowParams
{
	glm::vec4 sunDirectionWS{0.0f};    // xyz = direction, w = rayTMin
	glm::vec4 sunTangentWS{ 0.0f };    // xyz = tangent,   w = rayTMax
	glm::vec4 sunBitangentWS{ 0.0f };  // xyz = bitangent, w = mipBias
	glm::vec4 sunDirectionVS{ 0.0f };  // xyz = view-space direction

	float rayTMin = 0.001f;
	float rayTMax = 500.0f;
	float rayBias = 1e-4f;
	float normalBias = 0.03f;
};

struct alignas(16) RTShadowPush
{
	glm::vec2 resolution{ 0.0f };
	glm::vec2 invResolution{ 0.0f };

	RTShadowParams shadow{};

	uint32_t rayBase = 0u;
	uint32_t rayCapacity = 0u;
	uint32_t hilbertLutID = UINT32_MAX;

	float    saturationEps = 0.02f;
	float    disocclusionScale = 0.05f;
	uint32_t pad0[3]{};
};

struct alignas(16) ReflectPush
{
	glm::vec2 halfResSize{ 0.0f };
	glm::vec2 halfResTexel{ 0.0f };

	RTShadowParams shadow{ .rayTMax = 60.0f, .rayBias = 1e-4f, .normalBias = 0.06f };

	float reflectRoughnessCutoff = 0.60f;
	float roughnessFadeStart = 0.45f;
	float ambientScale = 1.0f;
	float bounceRoughnessCutoff = 0.35f;

	uint32_t noiseIndex = 0u;
	uint32_t hilbertLutID = UINT32_MAX;
	uint32_t skyboxID = UINT32_MAX;
	uint32_t brdfID = UINT32_MAX;

	uint32_t specularID = UINT32_MAX;
	uint32_t maxBounces = 3u;
	uint32_t maxReflectLights = 250u;
	uint32_t rayCapacity = 0u;

	uint32_t rayBase = 0u;

	float shadowSkipThreshold = 0.01f;
	uint32_t pad0[2]{};
};