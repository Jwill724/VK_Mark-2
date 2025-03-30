//#define TINYOBJLOADER_IMPLEMENTATION
//#include <tiny_obj_loader/tiny_obj_loader.h>

#include "Model.h"

//void Model::loadModel(const std::string& path, VkDevice device, VkCommandPool commandPool, VkQueue queue) {
//
//	tinyobj::attrib_t attrib;
//	std::vector<tinyobj::shape_t> shapes;
//	std::vector<tinyobj::material_t> materials;
//	std::string warn, err;
//
//	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str())) {
//		std::cerr << (warn + err) << std::endl;
//		exit(EXIT_FAILURE);
//	}
//
//	for (const auto& shape : shapes) {
//		Mesh mesh;
//		std::unordered_map<Vertex, uint32_t> uniqueVertices{};
//
//		for (const auto& index : shape.mesh.indices) {
//			Vertex vertex{};
//
//			vertex.pos = {
//				attrib.vertices[3 * index.vertex_index + 0],
//				attrib.vertices[3 * index.vertex_index + 1],
//				attrib.vertices[3 * index.vertex_index + 2]
//			};
//
//			if (index.texcoord_index >= 0) {
//				vertex.texCoord = {
//					attrib.texcoords[2 * index.texcoord_index + 0],
//					1.0f - attrib.texcoords[2 * index.texcoord_index + 1] // Flip Y for vulkan
//				};
//			}
//			else {
//				vertex.texCoord = { 0.0f, 0.0f };
//			}
//
//			vertex.color = { 1.0f, 1.0f, 1.0f };
//
//			if (uniqueVertices.count(vertex) == 0) {
//				uniqueVertices[vertex] = static_cast<uint32_t>(mesh.vertices.size());
//				mesh.vertices.push_back(vertex);
//			}
//
//			mesh.indices.push_back(uniqueVertices[vertex]);
//		}
//
//		meshes.push_back(mesh);
//	}
//
//	for (auto& mesh : meshes) {
//		mesh.createVertexBuffer(device, commandPool, queue);
//		mesh.createIndexBuffer(device, commandPool, queue);
//
//		vertexBuffers.push_back(mesh.vertexBuffer);
//		indexBuffers.push_back(mesh.indexBuffer);
//	}
//
////	createBuffers(device, commandPool, queue);
//}
//
//// Should only be transfer command pool and queue
//void Model::createBuffers(VkDevice device, VkCommandPool commandPool, VkQueue queue) {
//	for (auto& mesh : meshes) {
//		mesh.createVertexBuffer(device, commandPool, queue);
//		mesh.createIndexBuffer(device, commandPool, queue);
//
//		vertexBuffers.push_back(mesh.vertexBuffer);
//		indexBuffers.push_back(mesh.indexBuffer);
//	}
//}
//
//void Model::cleanup(VkDevice device) {
//	for (auto& mesh : meshes) {
//		mesh.cleanup(device);
//	}
//}