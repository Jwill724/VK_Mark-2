#pragma once

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define GLFW_EXPOSE_NATIVE_WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

#include <iostream>
#include <memory>
#include <optional>
#include <chrono>
#include <thread>
#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <set>
#include <array>
#include <functional>
#include <deque>
#include <queue>
#include <cmath>
#include <numeric>
#include <random>
#include <cstdint>#include <mutex>
#include <utility>
#include <unordered_set>#include <unordered_map>
#include <atomic>
#include <algorithm>
#include <variant>

#include "vulkan/Vulkan.h"
#include "glfw/glfw3.h"
#include "fmt/core.h"
#include <vma/vk_mem_alloc.h>
#include <vulkan/vk_enum_string_helper.h>
#include <stb_image/stb_image.h>
#include "enkiTS/TaskScheduler.h"
#include <meshoptimizer.h>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>
#endif

#define FASTGLTF_ENABLE_GLMC
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/util.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include "common/glm_common.hpp"

#include "assets/AreaTex.h"
#include "assets/SearchTex.h"

#ifndef NDEBUG
	#include <cassert>
	#define ASSERT(x) assert(x)
#else
	#define ASSERT(x) ((void)0)
#endif
