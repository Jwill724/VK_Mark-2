#pragma once

#include "common/Vk_Types.h"
#include "renderer/types/Mesh.h"

struct Model {
	std::vector<Mesh> meshes;
	glm::mat4 transform;
	std::vector<VkBuffer> vertexBuffers;
	std::vector<VkBuffer> indexBuffers;

	Model() = default;

	void loadModel(const std::string& path, VkDevice device, VkCommandPool commandPool, VkQueue queue);
	void createBuffers(VkDevice device, VkCommandPool commandPool, VkQueue queue);
	void cleanup(VkDevice device);
};