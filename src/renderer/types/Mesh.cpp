#include "Mesh.h"


//
//
//
//void Mesh::createVertexBuffer(VkDevice device, VkCommandPool commandPool, VkQueue queue) {
//
//	vertexBufferSize = sizeof(vertices[0]) * vertices.size();
//
//	BufferUtils::createBuffer(device, vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
//		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
//		stagingBuffer, stagingBufferMemory);
//
//	void* vertexData;
//	vkMapMemory(device, stagingBufferMemory, 0, vertexBufferSize, 0, &vertexData);
//	memcpy(vertexData, vertices.data(), (size_t)vertexBufferSize);
//	vkUnmapMemory(device, stagingBufferMemory);
//
//
//	BufferUtils::createBuffer(device, vertexBufferSize,
//		VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
//		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
//		vertexBuffer, vertexBufferMemory);
//
//	BufferUtils::copyBuffer(stagingBuffer, vertexBuffer, vertexBufferSize, commandPool, queue);
//
//	vkDestroyBuffer(device, stagingBuffer, nullptr);
//	vkFreeMemory(device, stagingBufferMemory, nullptr);
//}
//
//void Mesh::createIndexBuffer(VkDevice device, VkCommandPool commandPool, VkQueue queue) {
//	indexBufferSize = sizeof(indices[0]) * indices.size();
//
//	BufferUtils::createBuffer(device, indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
//		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
//		indexStagingBuffer, indexStagingBufferMemory);
//
//	void* indicesData;
//	vkMapMemory(device, indexStagingBufferMemory, 0, indexBufferSize, 0, &indicesData);
//	memcpy(indicesData, indices.data(), (size_t)indexBufferSize);
//	vkUnmapMemory(device, indexStagingBufferMemory);
//
//	BufferUtils::createBuffer(device, indexBufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
//		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
//		indexBuffer, indexBufferMemory);
//
//	BufferUtils::copyBuffer(indexStagingBuffer, indexBuffer, indexBufferSize, commandPool, queue);
//
//	vkDestroyBuffer(device, indexStagingBuffer, nullptr);
//	vkFreeMemory(device, indexStagingBufferMemory, nullptr);
//}
//
//void Mesh::cleanup(VkDevice device) {
//
//	vkDestroyBuffer(device, indexBuffer, nullptr);
//	vkFreeMemory(device, indexBufferMemory, nullptr);
//
//	vkDestroyBuffer(device, vertexBuffer, nullptr);
//	vkFreeMemory(device, vertexBufferMemory, nullptr);
//}
//
//// Vertex stuff
//VkVertexInputBindingDescription Vertex::getBindingDescription() {
//	VkVertexInputBindingDescription bindingDescription{};
//	bindingDescription.binding = 0;
//	bindingDescription.stride = sizeof(Vertex);
//	bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
//
//	return bindingDescription;
//}
//
//// Vertex setup
//std::array<VkVertexInputAttributeDescription, 3> Vertex::getAttributeDescriptions() {
//	// position attribute
//	std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};
//
//	attributeDescriptions[0].binding = 0;
//	attributeDescriptions[0].location = 0;
//	attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT; // vec3, the byte size
//	attributeDescriptions[0].offset = offsetof(Vertex, pos);
//
//	// color attribute
//	attributeDescriptions[1].binding = 0;
//	attributeDescriptions[1].location = 1;
//	attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
//	attributeDescriptions[1].offset = offsetof(Vertex, color);
//
//	attributeDescriptions[2].binding = 0;
//	attributeDescriptions[2].location = 2;
//	attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
//	attributeDescriptions[2].offset = offsetof(Vertex, texCoord);
//
//	return attributeDescriptions;
//}