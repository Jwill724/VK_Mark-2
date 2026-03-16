#pragma once

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include "EngineConstants.h"

#include <functional>
#include <deque>
#include <queue>
#include <optional>
#include <unordered_set>
#include <unordered_map>
#include <span>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <mutex>
#include <atomic>
#include <fmt/base.h>
#include <fmt/format.h>
#include "Bounds.h"

// SSBOs

struct Vertex {
	glm::vec3 position = glm::vec3(0.0f);

	int16_t normalX = 0;
	int16_t normalY = 0;

	uint16_t uvX = 0;
	uint16_t uvY = 0;

	uint32_t colorRGBA8 = 0xFFFFFFFFu;
};

// An instance basically = mesh
struct GPUInstance {
	uint32_t instanceID   = UINT32_MAX;  // Unique runtime tag for a renderables list *not current used
	uint32_t meshID       = UINT32_MAX;  // global meshBuffer
	uint32_t materialID   = UINT32_MAX;  // global material buffer
	uint32_t transformID  = UINT32_MAX;  // global transform/prevTransform buffer
	uint32_t passType     = UINT32_MAX;  // opaque/transparent material pass
};

// Meshes, materials all gpu ready at render

struct GPUMeshData {
	AABB localAABB;
	uint32_t firstIndex = UINT32_MAX;
	uint32_t indexCount = UINT32_MAX;
	uint32_t vertexOffset = UINT32_MAX;
	uint32_t vertexCount = UINT32_MAX;
	uint32_t shadowFirstIndex = UINT32_MAX;
	uint32_t shadowIndexCount = UINT32_MAX;
};

struct GPUMaterial {
	glm::vec4 colorFactor = glm::vec4(1.0f);
	glm::vec2 metalRoughFactors = glm::vec2(1.0f, 1.0f);

	float ambientOcclusion = 1.0f;
	float normalScale = 1.0f;

	glm::vec3 emissiveColor = glm::vec3(0.0f);
	float emissiveStrength = 1.0f;

	float alphaCutoff = 1.0f;
	uint32_t passType = 0;
	uint32_t flags = 0;

	uint32_t albedoID = UINT32_MAX;
	uint32_t metalRoughnessID = UINT32_MAX;
	uint32_t normalID = UINT32_MAX;
	uint32_t aoID = UINT32_MAX;
	uint32_t emissiveID = UINT32_MAX;
};

// GPU only buffers
enum class AddressBufferType : uint8_t {
	VisibleInstances,
	IndirectDraws,

	VisibleLightCount,
	VisibleLightIDs,

	ClusterCounts,
	ClusterOffsets,
	ClusterCursors,
	ClusterLightIDs,
	ClusterTileSliceRanges,
	ClusterScanScratch,

	Cmaa2Control,
	Cmaa2ShapeCandidates,
	Cmaa2DeferredLocations,
	Cmaa2DeferredItems,
	Cmaa2DeferredHeads,

	DispatchIndirectArgs,

	Lights,
	Transforms,
	PrevTransforms,
	Material,
	Mesh,
	Vertex,
	Index,
	Luminance,

	Count
};

// 100% bindless indirect table, stores gpu only, ssbo, and bda buffer pointers.
// Upload address table buffer after new addresses are attached or removed to the table.
struct alignas(16) GPUAddressTable {
	std::array<VkDeviceAddress, static_cast<size_t>(AddressBufferType::Count)> addrs{};

	void setAddress(AddressBufferType type, VkDeviceAddress address) {
		const size_t index = static_cast<size_t>(type);

		if (addrs[index] == address) {
			return;
		}

		addrs[index] = address;
		addressTableDirty = true;
	}

	void removeAddress(AddressBufferType type) {
		const size_t index = static_cast<size_t>(type);

		if (addrs[index] == 0) {
			return;
		}

		addrs[index] = 0;
		addressTableDirty = true;
	}

	bool isTableDirty() const {
		return addressTableDirty;
	}

	void clearTableDirty() {
		addressTableDirty = false;
	}

private:
	bool addressTableDirty = false;
};

// UNIFORM BUFFER TYPES
struct alignas(16) GPUSceneData {
	glm::mat4 view{};
	glm::mat4 proj{};
	glm::mat4 invView{};
	glm::mat4 invProj{};
	glm::mat4 viewproj{};
	glm::mat4 prevViewproj{};
	glm::uvec4 temporal{};         // x = frameIndex, y = historyValid (0/1), z = Hi-Z valid(0/1)
	glm::vec4 sunlightDirection{}; // w for sun power
	glm::vec4 sunlightColor{};
	glm::vec4 cameraPos{};
	glm::vec4 cameraClips{};       // .x near and .y far, .z invScreenWidth, .w invScreenHeight
	glm::vec4 viewportSize{};      // .x and .y for width and height, .z for pixel count
	glm::vec4 pixelSizes{};        // .x/.y = 1 / full extent .z/.w = = 1 / half extent
	glm::vec4 pad0{};
};

// x = diffuse, y = specular, z = brdf, w = skybox
struct alignas(16) GPUEnvMapIndexArray {
	glm::uvec4 indices[MAX_ENV_SETS];
};

struct alignas(16) GPUShadowCSM {
	glm::mat4 cascadeVP[MAX_SHADOW_CASCADES]{0.0f};
	glm::vec4 cascadeSplits{0.0f};
	// x=bias, y=shadowAtlasID, z=cascadeCount, w=atlasTexelSize
	glm::vec4 params{ 0.0f };
	// xy = uvScale, zw = uvOffset (per cascade)
	glm::vec4 atlasUV[MAX_SHADOW_CASCADES]{};
	glm::vec4 maxFilterRadiusTexels{};
};

struct TimelineSync {
	VkSemaphore semaphore = VK_NULL_HANDLE;
	uint64_t signalValue = UINT64_MAX;
};

enum class MaterialPass : uint32_t {
	Opaque,
	Transparent
};

// Instance drawing counts and transforms
enum class DrawType : uint32_t {
	DrawStatic,      // single baked instance
	DrawMultiStatic, // many baked instances
	DrawDynamic,     // single instance, dynamic transform
	DrawMultiDynamic // many instances, dynamic transforms
};

enum AOMode : uint32_t {
	AO_OFF,
	AO_GTAO // Ground Truth Ambient Occlusion
};

enum AAMode : uint32_t {
	AA_OFF,
	AA_CMAA2,   // Conservative Morphological Anti-Aliasing 2
	AA_SMAA,    // Sub-Pixel Morphological Anti-Aliasing
	AA_FXAA,    // Fast Approximate Anti-Aliasing
};

template<typename T>
inline void printVec3(const glm::vec<3, T>& v) {
	fmt::println("[{}, {}, {}]", v.x, v.y, v.z);
}

template<typename T>
inline void printMat4(const glm::mat<4, 4, T>& m) {
	for (int i = 0; i < 4; ++i) {
		fmt::println("[{}, {}, {}, {}]", m[i].x, m[i].y, m[i].z, m[i].w);
	}
}
