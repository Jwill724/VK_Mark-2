#pragma once

#include "common/Vk_Types.h"

struct RenderObject {
	uint32_t indexCount;
	uint32_t firstIndex;
	VkBuffer indexBuffer;

	MaterialInstance* material;
	AABB aabb;
	glm::mat4 transform;
	VkDeviceAddress vertexBufferAddress;
};

namespace RenderGraph {
	bool isVisible(const AABB& aabb, const Frustum& frus);
	bool boxInFrustum(const Frustum& fru, const AABB& box);
	AABB transformAABB(const AABB& localBox, const glm::mat4& transform);
	Frustum extractFrustum(const glm::mat4& viewproj);
	std::vector<glm::vec3> GetAABBVertices(const AABB& box);
}

struct DrawContext {
	std::vector<RenderObject> OpaqueSurfaces;
	std::vector<RenderObject> TransparentSurfaces;
	Frustum frustum;
	bool enableCull = true;
};

struct MeshNode : public Node {

	std::shared_ptr<MeshAsset> mesh;

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) override;
};