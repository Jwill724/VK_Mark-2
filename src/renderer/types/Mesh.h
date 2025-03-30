#pragma once
//#define GLM_ENABLE_EXPERIMENTAL
//#include <glm/gtx/hash.hpp>
//#include <glm/glm.hpp>
//#include <unordered_map>
//#include "utils/BufferUtils.h"
//#include "common/Vk_Types.h"

//struct Vertex {
//	glm::vec3 pos{};
//	glm::vec3 color{};
//	glm::vec2 texCoord{};
//
//	bool operator==(const Vertex& other) const {
//		return pos == other.pos && color == other.color && texCoord == other.texCoord;
//	}
//
//	// TODO: Store multiple buffers (vertex/index) into a VkBuffer and use offsets
//		// Aliasing is the term
//
//	static VkVertexInputBindingDescription getBindingDescription();
//	static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions();
//};

//namespace std {
//	template<> struct hash<Vertex> {
//		size_t operator()(const Vertex& vertex) const {
//			return ((hash<glm::vec3>()(vertex.pos) ^
//				(hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
//				(hash<glm::vec2>()(vertex.texCoord) << 1);
//		}
//	};
//}

struct Mesh {
	//std::vector<Vertex> vertices;
	//std::vector<uint32_t> indices;

	//VkBuffer vertexBuffer = VK_NULL_HANDLE;
	//VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
	//VkBuffer indexBuffer = VK_NULL_HANDLE;
	//VkDeviceMemory indexBufferMemory = VK_NULL_HANDLE;

	//VkBuffer stagingBuffer = VK_NULL_HANDLE;
	//VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
	//VkBuffer indexStagingBuffer = VK_NULL_HANDLE;
	//VkDeviceMemory indexStagingBufferMemory = VK_NULL_HANDLE;

	//uint32_t indexCount = 0;

	//VkDeviceSize vertexBufferSize = 0;
	//VkDeviceSize indexBufferSize = 0;

	//void createVertexBuffer(VkDevice device, VkCommandPool commandPool, VkQueue queue);
	//void createIndexBuffer(VkDevice device, VkCommandPool commandPool, VkQueue queue);

//	GPUMeshBuffers uploadMeshes(std::span<uint32_t> indices, std::span<Vertex> vertices);

//	void cleanup(VkDevice device);
};