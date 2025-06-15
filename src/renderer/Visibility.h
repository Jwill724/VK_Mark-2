#pragma once

#include "common/ResourceTypes.h"

namespace Visibility {
	bool isVisible(const AABB& aabb, const Frustum& frus);
	bool boxInFrustum(const Frustum& fru, const AABB& box);
	AABB transformAABB(const AABB& localBox, const glm::mat4& transform);
	Frustum extractFrustum(const glm::mat4& viewproj);
	std::vector<glm::vec3> GetAABBVertices(const AABB& box);
}