#pragma once

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

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

struct alignas(16) Frustum {
	glm::vec4 planes[6]; // Plane equation: ax + by + cz + d = 0
	float pad0[4];
	glm::vec4 points[8];
};

struct alignas(16) AABB {
	glm::vec3 vmin; // origin: 0.5f * (vmin + vmax)
	float pad0;

	glm::vec3 vmax; // extent: 0.5f * (vmax - vmin)
	float pad1;

	glm::vec3 origin;
	float pad2;

	glm::vec3 extent;
	float sphereRadius;
};

struct alignas(16) Vertex {
	glm::vec3 position;
	float pad0;

	glm::vec2 uv;
	glm::vec2 pad1;

	glm::vec3 normal;
	float pad2;

	glm::vec4 color;
	uint32_t pad3[4];
};

struct alignas(16) DrawPushConstants {
	uint32_t materialIndex;
	uint64_t vertexAddress;
	uint64_t indexAddress;
	glm::mat4 modelMatrix;
	uint32_t drawRangeIndex;
	uint32_t pad[3];
};

struct alignas(16) GPUDrawRange {
	uint32_t firstIndex;
	uint32_t indexCount;
	uint32_t vertexOffset;
	uint32_t vertexCount;
};

struct alignas(16) InstanceData {
	glm::mat4 modelMatrix;
	AABB localAABB;

	uint32_t materialIndex;
	uint32_t drawRangeIndex;
	uint32_t pad0[2];

	uint64_t vertexBufferAddress;
	uint64_t indexBufferAddress;

	uint32_t pad[4];
};
static_assert(sizeof(InstanceData) == 176);
static_assert(offsetof(InstanceData, vertexBufferAddress) % 8 == 0);

struct alignas(16) PBRMaterial {
	glm::vec4 colorFactor = glm::vec4(1.0f);
	glm::vec2 metalRoughFactors = glm::vec2(1.0f, 1.0f);
	float pad0[2] = { 0.f, 0.f };

	uint32_t albedoLUTIndex;
	uint32_t metalRoughLUTIndex;
	uint32_t normalLUTIndex;
	uint32_t aoLUTIndex;

	glm::vec3 emissiveColor = glm::vec3(0.0f);
	float emissiveStrength = 1.0f;

	float ambientOcclusion = 1.0f;
	float normalScale = 1.0f;
	float alphaCutoff = 1.0f;
	float pad3 = 0.0f;
};
static_assert(sizeof(PBRMaterial) == 80);

struct alignas(16) GPUSceneData {
	glm::mat4 view;
	glm::mat4 proj;
	glm::mat4 viewproj;
	glm::vec4 ambientColor;
	glm::vec4 sunlightDirection; // w for sun power
	glm::vec4 sunlightColor;
	glm::vec4 cameraPosition;
	glm::vec4 envMapIndex; // x = diffuse, y = specular, z = brdf, w = skybox
};

struct alignas(16) GPUAddressTable {
	VkDeviceAddress instanceBuffer;
	VkDeviceAddress indirectCmdBuffer;
	VkDeviceAddress drawRangeBuffer;
	VkDeviceAddress materialBuffer;
};

struct alignas(16) IndirectDrawCmd {
	VkDrawIndexedIndirectCommand cmd;
	uint32_t instanceIndex;
	uint32_t drawOffset;
	uint32_t pad0;
};

struct RenderSyncObjects {
	VkSemaphore swapchainSemaphore = VK_NULL_HANDLE;
	VkSemaphore semaphore = VK_NULL_HANDLE;
	VkFence fence = VK_NULL_HANDLE;
};

struct TimelineSync {
	VkSemaphore semaphore = VK_NULL_HANDLE;
	uint64_t signalValue = UINT64_MAX;
};

enum class MaterialPass : uint8_t {
	Opaque,
	Transparent,
	ColorPostProcess,
	Specular,
	Diffuse,
	Brdf,
	Shadow,
	Wireframe,
	Overlay,
};

enum class BufferType : uint8_t {
	Vertex,
	Index,
	Uniform,             // UBO
	Storage,             // SSBO
	MaterialData,
	InstanceData,
	IndirectDraw,
	Staging,
	SceneConstants,
	ImageLUT,
	LightData,
};


enum class RenderImageType : uint8_t {
	Albedo,              // G-Buffer base color
	Normals,             // G-Buffer normals (world or view space)
	MetalRoughAO,        // Packed metalness, roughness, AO
	Emissive,            // Optional emissive G-buffer
	Depth,               // Shared depth buffer
	LightingAccum,       // Deferred lighting accumulation buffer
	FinalOutput,         // Swapchain-ready image
	PostProcess,         // Temporary post-processing buffer
	ShadowMap,           // Depth-only shadow map
	BRDFLUT,             // BRDF lookup texture
	EnvDiffuse,          // Diffuse irradiance cubemap
	EnvSpecular,         // Prefiltered specular cubemap
};

struct alignas(16) ColorData {
	float brightness = 0.0f;
	float saturation = 0.0f;
	float contrast = 0.0f;
	float pad0 = 0.0f;
	uint32_t cmbViewIdx = 0;
	uint32_t storageViewIdx = 0;
	uint32_t pad1[2];
};

// Checking if the vector is out of range for aabb vertices
inline auto isFiniteVec3 = [](const glm::vec3& v) {
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
};