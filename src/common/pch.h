#pragma once

#include <iostream>
#include <memory>
#include <optional>
#include <chrono>
#include <string>
#include <vector>
#include <span>
#include <array>
#include <functional>
#include <deque>
#include "fmt/core.h"

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#include <vulkan/vk_enum_string_helper.h>

#include <stb_image/stb_image.h>
#include <GLFW/glfw3.h>

#define FASTGLTF_ENABLE_GLMC
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/util.hpp>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"

#include "ErrorChecking.h"
#include "file/File.h"

#include "common/glm_common.hpp"