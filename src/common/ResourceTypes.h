#pragma once

#include "renderer/RendererDefinitions.h"
#include <vector>
#include <memory>
#include <Core.h>

namespace RD = RendererDefinitions;

// Virtual control over instances, enables true instancing with unique transforms
struct VirtualInstance
{
	uint32_t instanceID = UINT32_MAX;         // The singular model index tag
	uint8_t sceneID = UINT8_MAX;              // Unordered map id to map this back to loadedScenes
	RD::InstancingMethod instancingMethod =
		RD::InstancingMethod::DrawStatic;     // Controls how an asset is treated in drawing
	glm::vec3 modelOffset{ 0.0f };            // Divides spacing in world space between models

	// Only first transform should change at runtime to move through the global transform vector
	glm::mat4 baseTransform = glm::mat4(0.0f); // First transform matrix in global list
	uint32_t firstTransform = 0;               // start of this instance's transform slab (copy 0, slot 0)
	uint32_t transformCount = 0;               // transforms PER COPY (unique node slots)
	uint32_t perInstanceStride = 0;            // rows PER COPY (meshes/primitives in bakedInstances)

	uint32_t usedCopies = 1;                   // active copies (drawn / in InstanceState)
	uint32_t capacityCopies = 1;               // allocated copies in transform slab (contiguous storage)

	float spinAngleRadians = 0.0f;
	float movePhaseRadians = 0.0f;

	uint32_t flagsForce = 0u;
	uint32_t flagsMask = ~0u;
};

struct BinKey
{
	uint32_t meshID;      // INVALID_U32 = empty slot
	uint32_t materialID;
	uint32_t binID;
};

struct DrawBinKeys
{
	std::vector<BinKey>     hashTable;  // BIN_TABLE_SIZE entries, open addressing
	std::vector<glm::uvec2> denseKeys;  // binID -> {meshID, materialID}, MAX_DRAW_BINS entries
};

struct BinTableBuild
{
	DrawBinKeys binKeys;
	uint32_t binCount = 0;
};

// must match the GLSL hash bit for bit
static uint32_t BinHash(uint32_t meshID, uint32_t materialID)
{
	return ((meshID * 2654435761u) ^ (materialID * 2246822519u)) & (RD::BIN_TABLE_SIZE - 1u);
}

struct AsteroidState
{
	glm::vec3 basePosition;
	float     scale;

	glm::vec3 tumbleAxis;
	float     tumbleRate;      // radians/sec
	float     tumblePhase;

	glm::vec3 driftAmplitude;  // world units
	glm::vec3 driftFrequency;  // Hz
	glm::vec3 driftPhase;
};

struct Node
{
	std::weak_ptr<Node> parent;
	std::vector<std::shared_ptr<Node>> children;

	glm::mat4 localTransform{ 1.0f };
	glm::mat4 worldTransform{ 1.0f };

	constexpr void RefreshTransform(const glm::mat4& parentMatrix) noexcept
	{
		worldTransform = parentMatrix * localTransform;
		for (auto& c : children)
			if (c) c->RefreshTransform(worldTransform);
	}
};

// For use in bend studios screen space contact shadows
struct DispatchData {
	glm::ivec3 waveCount{0};   // Dispatch values passed to compute scope
	glm::ivec2 waveOffset{0};  // Offset passed to push constant equivalent
};
// For use in bend studios screen space contact shadows
struct DispatchList {
	glm::vec4 lightCoords{0.0f}; // Same between all disptaches

	DispatchData dispatch[8]{};
	int dispatchCount = 0;
};

// UNIFORM BUFFER TYPES
struct alignas(16) SceneInfo
{
	glm::mat4 view{};
	glm::mat4 proj{};
	glm::mat4 projUnjittered{};
	glm::mat4 invView{};
	glm::mat4 invProj{};
	glm::mat4 viewProj{};
	glm::mat4 prevViewProj{};
	glm::mat4 prevView{};
	glm::mat4 viewProjUnjittered{};
	// x = frameNumber, y = historyValid (0/1), z = Hi-Z valid(0/1)
	glm::uvec4 temporal{};
	// x = current jitter x ndc
	// y = current jitter y
	// z = previous jitter x
	// w = previous jitter y
	glm::vec4 temporalJitter{};
	// w for sun power
	glm::vec4 sunlightDirection{};
	glm::vec4 sunlightColor{};
	glm::vec4 cameraPos{};         // xyz pos
	glm::vec4 cameraClips{};       // .x near and .y far
	glm::vec4 viewportSize{};      // .x and .y for width and height, .z for pixel count
	glm::vec4 pixelSizes{};        // .x/.y = 1 / full m_extent .z/.w = = 1 / half m_extent
	glm::mat4 flashlightVP{};
};

struct alignas(16) DirectionalCSMInfo
{
	glm::mat4 cascadeVP[RD::MAX_SHADOW_CASCADES]{0.0f};
	glm::mat4 cascadeLightViews[RD::MAX_SHADOW_CASCADES]{0.0f};
	glm::vec4 cascadeSplits{0.0f};
	// x=shadowAtlasID, y=cascadeCount, z=atlasTexelSize, w=eplison
	glm::vec4 params{ 0.0f };
	// xy = uvScale, zw = uvOffset (per cascade)
	glm::vec4 atlasUV[RD::MAX_SHADOW_CASCADES]{};
	glm::vec4 maxFilterRadiusTexels{};
	glm::vec4 cascadeWorldTexels{};
};

struct ShadowControl
{
	float splitLambda                = 0.97f;
	float bias                       = 0.0001f;
	float shadowFar                  = 1000.0f;
	float lsEpsilon                  = 1.0f;
	// Doubles the far depth range
	bool enableShadowDepthExtendHack = false;
};
