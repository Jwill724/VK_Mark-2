cmake_minimum_required(VERSION 3.22)

project(VulkanRendererNRD LANGUAGES CXX)

include(FetchContent)

# ============================================================
# NRD configuration
# ============================================================

set(NRD_STATIC_LIBRARY ON CACHE BOOL "" FORCE)
set(NRD_NRI OFF CACHE BOOL "" FORCE)

set(NRD_EMBEDS_DXBC_SHADERS OFF CACHE BOOL "" FORCE)
set(NRD_EMBEDS_DXIL_SHADERS OFF CACHE BOOL "" FORCE)
set(NRD_EMBEDS_SPIRV_SHADERS ON CACHE BOOL "" FORCE)

# ============================================================
# Fetch NRD 4.17.4
# ============================================================

FetchContent_Declare(
    NRDSDK

    GIT_REPOSITORY
        https://github.com/NVIDIA-RTX/NRD.git

    # NRD 4.17.4
    GIT_TAG
        9a3fe938a7558fd16b6c91a1c0456305cdcd9f16
)

FetchContent_MakeAvailable(NRDSDK)

# ============================================================
# Vendor output
# ============================================================

set(
    NRD_VENDOR_LIB_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../vendor/nrd/Lib"
)

# Build NRD and copy the resulting libraries into:
#
# vendor/nrd/Lib/Debug
# vendor/nrd/Lib/Release
#
add_custom_target(
    nrd_vendor

    COMMAND
        ${CMAKE_COMMAND} -E make_directory
        "${NRD_VENDOR_LIB_DIR}/$<CONFIG>"

    COMMAND
        ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:NRD>"
        "${NRD_VENDOR_LIB_DIR}/$<CONFIG>/NRD.lib"

    COMMAND
        ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:ShaderMakeBlob>"
        "${NRD_VENDOR_LIB_DIR}/$<CONFIG>/ShaderMakeBlob.lib"

    DEPENDS
        NRD
        ShaderMakeBlob

    COMMENT
        "Copying NRD libraries to vendor/nrd/Lib/$<CONFIG>"
)