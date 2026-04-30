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
#include <vector>
#include <span>
#include <array>
#include <functional>
#include <deque>
#include <cmath>
#include <numeric>
#include <random>

#define GLFW_INCLUDE_VULKAN
#include "glfw/glfw3.h"

#include "fmt/core.h"
#include <vma/vk_mem_alloc.h>
#include <vulkan/vk_enum_string_helper.h>
#include <stb_image/stb_image.h>
#include "enkiTS/TaskScheduler.h"
#include "conjure_enum/conjure_enum.hpp"
//#include "conjure_enum/conjure_enum_bitset.hpp"
//#include "conjure_enum/conjure_enum_ext.hpp"
//#include "conjure_enum/conjure_type.hpp"

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

#include "renderer/backend/VulkanTypes.h"
#include "common/glm_common.hpp"
#include "EngineConstants.h"

#include "assets/AreaTex.h"
#include "assets/SearchTex.h"
