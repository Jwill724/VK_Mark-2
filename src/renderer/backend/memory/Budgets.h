#pragma once

#include "renderer/RendererDefinitions.h"

namespace RD = RendererDefinitions;

// Can get risky? Keep this updated at all cost!
inline constexpr size_t INSTANCE_SIZE     = 16;
inline constexpr size_t INDIRECT_CMD_SIZE = 20;
inline constexpr size_t LOCAL_LIGHT_SIZE  = 60;
inline constexpr size_t MAT4_SIZE         = 64;

inline constexpr size_t MAX_INSTANCE_SIZE_GPU_BYTES        = RD::MAX_FRAME_INSTANCES_TOTAL       * INSTANCE_SIZE;
inline constexpr size_t MAX_INDIRECT_SIZE_GPU_BYTES        = RD::MAX_FRAME_DRAW_COMMANDS_TOTAL   * INDIRECT_CMD_SIZE;
inline constexpr size_t MAX_VISIBLE_IDS_SIZE_GPU_BYTES     = RD::MAX_FRAME_INSTANCES_TOTAL       * sizeof(uint32_t);
inline constexpr size_t MAX_TRANSFORMS_SIZE_GPU_BYTES      = RD::MAX_INSTANCE_TRANSFORMS         * MAT4_SIZE;
inline constexpr size_t MAX_LIGHTS_SIZE_GPU_BYTES          = RD::MAX_LIGHTS                      * LOCAL_LIGHT_SIZE;
inline constexpr size_t MAX_LIGHT_IDS_SIZE_GPU_BYTES       = static_cast<size_t>(RD::MAX_LIGHTS) * sizeof(uint32_t);
inline constexpr size_t MIN_SSBO_ALIGNMENT_BYTES           = 256;
inline constexpr size_t LUMINANCE_GROUPS_SIZE_GPU_BYTES    = static_cast<size_t>(RD::MAX_LUMINANCE_GROUPS) * sizeof(glm::vec4);
