#pragma once

#include "common/ResourceTypes.h"

namespace Visibility {
	void performCulling(
		VkCommandBuffer cmd,
		CullingPushConstantsAddrs& cullingData,
		VkBuffer visibleCountStaging,
		VkBuffer visibleMeshIDsStaging,
		VkBuffer gpuVisibleCountBuffer,
		VkBuffer gpuVisibleMeshIDsBuffer,
		VkDescriptorSet sets[2]);

	bool isVisible(const AABB aabb, const Frustum frus);
	bool boxInFrustum(const AABB aabb, const Frustum frus);
	AABB transformAABB(const AABB& localBox, const glm::mat4& transform);
	Frustum extractFrustum(const glm::mat4 viewproj);
	std::vector<glm::vec3> GetAABBVertices(const AABB box);
}