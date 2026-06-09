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
	RD::InstancingMethod drawType =
		RD::InstancingMethod::DrawStatic;     // Controls how an asset is treated in drawing
	glm::vec3 modelOffset{ 0.0f };            // Divides spacing in world space between models

	// Only first transform should change at runtime to move through the global transform vector
	glm::mat4 baseTransform = glm::mat4(0.0f); // First transform matrix in global list
	uint32_t firstTransform = 0;               // start of this instance's transform slab (copy 0, slot 0)
	uint32_t transformCount = 0;               // transforms PER COPY (unique node slots)
	uint32_t perInstanceStride = 0;            // rows PER COPY (meshes/primitives in bakedInstances)

	uint32_t usedCopies = 1;                   // active copies (drawn / in VisibilityState)
	uint32_t capacityCopies = 1;               // allocated copies in transform slab (contiguous storage)

	float spinAngleRadians = 0.0f;
	float movePhaseRadians = 0.0f;
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
};

struct alignas(16) DirectionalCSMInfo
{
	glm::mat4 cascadeVP[RD::MAX_SHADOW_CASCADES]{0.0f};
	glm::vec4 cascadeSplits{0.0f};
	// x=bias, y=shadowAtlasID, z=cascadeCount, w=atlasTexelSize
	glm::vec4 params{ 0.0f };
	// xy = uvScale, zw = uvOffset (per cascade)
	glm::vec4 atlasUV[RD::MAX_SHADOW_CASCADES]{};
	glm::vec4 maxFilterRadiusTexels{};
	float cascadeBias[RD::MAX_SHADOW_CASCADES]{};
	//float cascadeNormalOffset[MAX_SHADOW_CASCADES]{};
};

struct ShadowControl
{
	float splitLambda          = 0.97f;
	float bias                 = 0.0001f;
	float softnessFactor;
	float maxCasterDistance[4] = { 3000.0f, 4000.0f, 5000.0f, 6000.0f };
	float xyPadding            = 150.0f;
	float lsEpsilon            = 5.0f;
	float dirEpsilon           = 20.0f;
	float shadowRadii[4]       = { 17.0f, 46.0f, 160.0f, 1000.0f };
};
