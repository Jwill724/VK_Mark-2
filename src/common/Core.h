#pragma once

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <fmt/base.h>
#include <fmt/format.h>

#include <cstdint>

template<typename T>
inline void PrintVec3(const glm::vec<3, T>& v) {
	fmt::println("[{}, {}, {}]", v.x, v.y, v.z);
}

template<typename T>
inline void PrintMat4(const glm::mat<4, 4, T>& m) {
	for (int i = 0; i < 4; ++i) {
		fmt::println("[{}, {}, {}, {}]", m[i].x, m[i].y, m[i].z, m[i].w);
	}
}
