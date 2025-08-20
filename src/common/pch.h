#pragma once

// Platform
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define GLFW_EXPOSE_NATIVE_WIN32
#include <windows.h>
#include <mmsystem.h>
#endif

// STL
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

// Third-party
#include "fmt/core.h"
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vma/vk_mem_alloc.h>
#include <vulkan/vk_enum_string_helper.h>
#include <stb_image/stb_image.h>

#define FASTGLTF_ENABLE_GLMC
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/util.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

// Project
#include "common/ErrorChecking.h"
#include "common/glm_common.hpp"

#include "enkiTS/TaskScheduler.h"