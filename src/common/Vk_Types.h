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

struct Frustum {
	glm::vec4 planes[6]; // Plane equation: ax + by + cz + d = 0
	glm::vec4 points[8];
};

struct AABB {
	glm::vec3 vmin; // origin: 0.5f * (vmin + vmax)
	glm::vec3 vmax; // extent: 0.5f * (vmax - vmin)
	glm::vec3 origin;
	glm::vec3 extent;
	float sphereRadius;
};

struct Vertex {
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 uv;
	glm::vec4 color;
};

struct GPUInstance {
	uint32_t instanceID = UINT32_MAX;
	uint32_t materialID = UINT32_MAX;
	uint32_t meshID = UINT32_MAX;
	uint32_t transformID = UINT32_MAX;
};

// Draw ranges, meshes, materials all gpu ready at render
struct GPUDrawRange {
	uint32_t firstIndex = UINT32_MAX;
	uint32_t indexCount = UINT32_MAX;
	uint32_t vertexOffset = UINT32_MAX;
	uint32_t vertexCount = UINT32_MAX;
};

struct GPUMeshData {
	AABB localAABB;
	AABB worldAABB;
	uint32_t drawRangeID = UINT32_MAX;
};

struct GPUMaterial {
	glm::vec4 colorFactor = glm::vec4(1.0f);
	glm::vec2 metalRoughFactors = glm::vec2(1.0f, 1.0f);

	glm::vec3 emissiveColor = glm::vec3(0.0f);
	float emissiveStrength = 1.0f;

	float ambientOcclusion = 1.0f;
	float normalScale = 1.0f;
	float alphaCutoff = 1.0f;
	uint32_t passType = 0;

	uint32_t albedoID = UINT32_MAX;
	uint32_t metalRoughnessID = UINT32_MAX;
	uint32_t normalID = UINT32_MAX;
	uint32_t aoID = UINT32_MAX;
	uint32_t emissiveID = UINT32_MAX;
};

// Uniforms

struct alignas(16) GPUSceneData {
	glm::mat4 view;
	glm::mat4 proj;
	glm::mat4 viewproj;
	glm::vec4 ambientColor;
	glm::vec4 sunlightDirection; // w for sun power
	glm::vec4 sunlightColor;
	glm::vec4 cameraPosition;
};
static_assert(sizeof(GPUSceneData) == 256);

// x = diffuse, y = specular, z = brdf, w = skybox
struct alignas(16) GPUEnvMapIndexArray {
	glm::uvec4 indices[MAX_ENV_SETS];
};
static_assert(sizeof(GPUEnvMapIndexArray) == MAX_ENV_SETS * sizeof(glm::uvec4));

// GPU only buffers
enum class AddressBufferType : uint8_t {
	OpaqueIntances,
	OpaqueIndirectDraws,
	TransparentInstances,
	TransparentIndirectDraws,
	Material,
	Mesh,
	DrawRange,
	Vertex,
	Index,
	Transforms,
	VisibleCount,
	VisibleMeshIDs,
	Count
};

struct alignas(128) GPUAddressTable {
	std::array<VkDeviceAddress, static_cast<size_t>(AddressBufferType::Count)> addrs;
	void setAddress(AddressBufferType t, VkDeviceAddress addr) {
		addrs[static_cast<size_t>(t)] = addr;
	}
};

struct TimelineSync {
	VkSemaphore semaphore = VK_NULL_HANDLE;
	uint64_t signalValue = UINT64_MAX;
};

enum class MaterialPass : uint8_t {
	Opaque,
	Transparent
};


// Push constant use
struct alignas(16) ColorData {
	float brightness = 0.0f;
	float saturation = 0.0f;
	float contrast = 0.0f;
	float pad0 = 0.0f;
	uint32_t cmbViewIdx = 0;
	uint32_t storageViewIdx = 0;
	uint32_t pad1[2]{};
};

template<typename T>
inline void printVec3(const glm::vec<3, T>& v) {
	fmt::print("[{}, {}, {}]", v.x, v.y, v.z);
}

template<typename T>
inline void printMat4(const glm::mat<4, 4, T>& m) {
	for (int i = 0; i < 4; ++i) {
		fmt::print("[{}, {}, {}, {}]\n", m[i].x, m[i].y, m[i].z, m[i].w);
	}
}