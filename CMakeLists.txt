cmake_minimum_required(VERSION 3.20)
project(VulkanRenderer LANGUAGES CXX)

# Use the latest standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Set Vulkan SDK path if available
if(NOT DEFINED ENV{VULKAN_SDK})
    message(FATAL_ERROR "VULKAN_SDK environment variable not set.")
endif()

set(VULKAN_SDK $ENV{VULKAN_SDK})

# Include dirs
include_directories(
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/common
    ${PROJECT_SOURCE_DIR}/vendor
    ${PROJECT_SOURCE_DIR}/vendor/Vulkan/include
    ${PROJECT_SOURCE_DIR}/vendor/simdjson
    ${PROJECT_SOURCE_DIR}/vendor/im_gui
    ${PROJECT_SOURCE_DIR}/vendor/enkiTS
)

# Link dirs
link_directories(
    ${PROJECT_SOURCE_DIR}/vendor/Vulkan/lib
    ${PROJECT_SOURCE_DIR}/vendor/GLFW/lib
)

# Gather sources
file(GLOB_RECURSE SRC_FILES
    src/*.cpp
    vendor/stb_image/*.cpp
    vendor/simdjson/*.cpp
    vendor/im_gui/*.cpp
    vendor/Vulkan/include/vma/vma.cpp
    vendor/enkiTS/TaskScheduler.cpp
)

# Executable target
add_executable(VulkanRenderer ${SRC_FILES})
target_compile_options(VulkanRenderer PRIVATE /utf-8)

# Precompiled header (Visual Studio only)
target_precompile_headers(VulkanRenderer PRIVATE src/common/pch.h)

# Link libraries
target_link_libraries(VulkanRenderer
    glfw3
    vulkan-1
    shaderc_combined
)

# Windows only: set subsystem (so you don't get a console window)
if (WIN32)
    set_target_properties(VulkanRenderer PROPERTIES
        LINK_FLAGS "/SUBSYSTEM:WINDOWS"
    )
endif()

target_compile_definitions(VulkanRenderer PRIVATE
    UNICODE
    _UNICODE
    VK_API_VERSION=VK_API_VERSION_1_3
)

# Post-build: compile shaders
add_custom_command(TARGET VulkanRenderer PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Compiling shaders..."
    COMMAND ${CMAKE_COMMAND} -E env VULKAN_SDK=$ENV{VULKAN_SDK}
            cmd /C compile_shaders.bat
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}/res/shaders
)