#pragma once

#include "common/Vk_Types.h"

struct RenderObject {
	uint32_t indexCount;
	uint32_t firstIndex;
	VkBuffer indexBuffer;

	MaterialInstance* material;
	Bounds bounds;
	glm::mat4 transform;
	VkDeviceAddress vertexBufferAddress;
};

bool is_visible(const RenderObject& obj, const glm::mat4& viewproj);

struct DrawContext {
	std::vector<RenderObject> OpaqueSurfaces;
	GPUSceneData sceneData;
};

struct MeshNode : public Node {

	std::shared_ptr<MeshAsset> mesh;

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) override;
};