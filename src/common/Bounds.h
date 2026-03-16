#pragma once

#include <vector>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

struct Frustum {
	glm::vec4 planes[6]; // Plane equation: ax + by + cz + d = 0
	//glm::vec4 corners[8];
};

struct AABB {
	glm::vec3 vmin; // origin: 0.5f * (vmin + vmax)
	glm::vec3 vmax; // extent: 0.5f * (vmax - vmin)
	glm::vec3 origin;
	glm::vec3 extent;
	float sphereRadius;
};

inline Frustum extractFrustum(const glm::mat4& viewproj) {
	const glm::mat4 vpt = glm::transpose(viewproj);

	Frustum frustum{};
	frustum.planes[0] = vpt[3] + vpt[0]; // left
	frustum.planes[1] = vpt[3] - vpt[0]; // right
	frustum.planes[2] = vpt[3] + vpt[1]; // bot
	frustum.planes[3] = vpt[3] - vpt[1]; // top
	frustum.planes[4] = vpt[3] + vpt[2]; // near
	frustum.planes[5] = vpt[3] - vpt[2]; // far

	for (int i = 0; i < 6; ++i) {
		frustum.planes[i] /= glm::length(glm::vec3(frustum.planes[i]));
	}

	//if (useCorners) {
	//    glm::mat4 invVp = glm::inverse(viewproj);
	   // int i = 0;
	   // for (int x = -1; x <= 1; x += 2) {
		  //  for (int y = -1; y <= 1; y += 2) {
			 //   for (int z = 0; z <= 1; z++) { // Vulkan NDC z [0,1]
				//    glm::vec4 cornerNDC = glm::vec4(
				//	    static_cast<float>(x),
				//	    static_cast<float>(y),
				//	    static_cast<float>(z),
				//	    1.0f
				//    );
				//    glm::vec4 cornerWorld = invVp * cornerNDC;
				//    frustum.corners[i++] = cornerWorld / cornerWorld.w;
			 //   }
		  //  }
	   // }
	//}

	return frustum;
}

// AABB transform methods https://ktstephano.github.io/rendering/stratusgfx/aabbs
inline AABB AABBtoWorldSpace(const AABB& localBox, const glm::mat4& transform) {
	// Convert to min/max corners first
	const glm::vec3 vmin = localBox.vmin;
	const glm::vec3 vmax = localBox.vmax;

	const glm::vec3 corners[8] = {
		glm::vec3(transform * glm::vec4(vmin.x, vmin.y, vmin.z, 1.0f)),
		glm::vec3(transform * glm::vec4(vmin.x, vmax.y, vmin.z, 1.0f)),
		glm::vec3(transform * glm::vec4(vmin.x, vmin.y, vmax.z, 1.0f)),
		glm::vec3(transform * glm::vec4(vmin.x, vmax.y, vmax.z, 1.0f)),
		glm::vec3(transform * glm::vec4(vmax.x, vmin.y, vmin.z, 1.0f)),
		glm::vec3(transform * glm::vec4(vmax.x, vmax.y, vmin.z, 1.0f)),
		glm::vec3(transform * glm::vec4(vmax.x, vmin.y, vmax.z, 1.0f)),
		glm::vec3(transform * glm::vec4(vmax.x, vmax.y, vmax.z, 1.0f))
	};

	// Now apply the min/max algorithm from before using the 8 transformed corners
	glm::vec3 newVmin = corners[0];
	glm::vec3 newVmax = newVmin;

	// Start looping from corner 1 onwards
	for (size_t i = 1; i < 8; ++i) {
		const auto& current = corners[i];
		newVmin = glm::min(newVmin, current);
		newVmax = glm::max(newVmax, current);
	}

	AABB worldBox{};
	worldBox.vmin = newVmin;
	worldBox.vmax = newVmax;
	worldBox.origin = (newVmax + newVmin) * 0.5f;
	worldBox.extent = (newVmax - newVmin) * 0.5f;
	worldBox.sphereRadius = glm::length(worldBox.extent);

	return worldBox;
}

inline std::vector<glm::vec3> GetAABBVertices(const AABB& box) {
	const glm::vec3 vmin = box.vmin;
	const glm::vec3 vmax = box.vmax;

	const glm::vec3 corners[8] {
		glm::vec3(vmin.x, vmin.y, vmin.z),
		glm::vec3(vmin.x, vmax.y, vmin.z),
		glm::vec3(vmin.x, vmin.y, vmax.z),
		glm::vec3(vmin.x, vmax.y, vmax.z),
		glm::vec3(vmax.x, vmin.y, vmin.z),
		glm::vec3(vmax.x, vmax.y, vmin.z),
		glm::vec3(vmax.x, vmin.y, vmax.z),
		glm::vec3(vmax.x, vmax.y, vmax.z)
	};

	// Now connect the corners to form 12 lines
	std::vector<glm::vec3> vertices {
		// edges along X
		corners[0], corners[1],
		corners[2], corners[3],
		corners[4], corners[5],
		corners[6], corners[7],

		// edges along Y
		corners[0], corners[2],
		corners[1], corners[3],
		corners[4], corners[6],
		corners[5], corners[7],

		// edges along Z
		corners[0], corners[4],
		corners[1], corners[5],
		corners[2], corners[6],
		corners[3], corners[7]
	};

	return vertices;
}

inline std::vector<glm::vec3> GetOBBVertices(
	const AABB& localBox,
	const glm::mat4& modelMatrix)
{
	const glm::vec3 vmin = localBox.vmin;
	const glm::vec3 vmax = localBox.vmax;

	const glm::vec3 localCorners[8] {
		glm::vec3(vmin.x, vmin.y, vmin.z),
		glm::vec3(vmin.x, vmax.y, vmin.z),
		glm::vec3(vmin.x, vmin.y, vmax.z),
		glm::vec3(vmin.x, vmax.y, vmax.z),
		glm::vec3(vmax.x, vmin.y, vmin.z),
		glm::vec3(vmax.x, vmax.y, vmin.z),
		glm::vec3(vmax.x, vmin.y, vmax.z),
		glm::vec3(vmax.x, vmax.y, vmax.z)
	};

	glm::vec3 worldCorners[8]{};
	for (uint32_t i = 0; i < 8; ++i) {
		glm::vec4 world = modelMatrix * glm::vec4(localCorners[i], 1.0f);
		worldCorners[i] = glm::vec3(world);
	}

	std::vector<glm::vec3> vertices {
		// edges along X
		worldCorners[0], worldCorners[1],
		worldCorners[2], worldCorners[3],
		worldCorners[4], worldCorners[5],
		worldCorners[6], worldCorners[7],

		// edges along Y
		worldCorners[0], worldCorners[2],
		worldCorners[1], worldCorners[3],
		worldCorners[4], worldCorners[6],
		worldCorners[5], worldCorners[7],

		// edges along Z
		worldCorners[0], worldCorners[4],
		worldCorners[1], worldCorners[5],
		worldCorners[2], worldCorners[6],
		worldCorners[3], worldCorners[7]
	};

	return vertices;
}
