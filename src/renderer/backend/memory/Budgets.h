#pragma once

//#include "Material.h"
#include "ResourceTypes.h"
#include "renderer/scene/LightingSystem.h"

namespace RD = RendererDefinitions;

inline constexpr size_t MAX_INSTANCE_SIZE_GPU_BYTES        = RD::MAX_FRAME_INSTANCES_TOTAL     * sizeof(Instance);
inline constexpr size_t MAX_INDIRECT_SIZE_GPU_BYTES        = RD::MAX_FRAME_DRAW_COMMANDS_TOTAL * sizeof(VkDrawIndexedIndirectCommand);
inline constexpr size_t MAX_VISIBLE_IDS_SIZE_GPU_BYTES     = RD::MAX_FRAME_INSTANCES_TOTAL     * sizeof(uint32_t);
inline constexpr size_t MAX_TRANSFORMS_SIZE_GPU_BYTES      = RD::MAX_INSTANCE_TRANSFORMS * sizeof(glm::mat4);
inline constexpr size_t MAX_LIGHTS_SIZE_GPU_BYTES          = RD::MAX_LIGHTS * sizeof(LocalLight);
inline constexpr size_t MAX_LIGHT_IDS_SIZE_GPU_BYTES       = static_cast<size_t>(MAX_VISIBLE_LIGHTS) * sizeof(uint32_t);
inline constexpr size_t GPU_ADDRESS_TABLE_SIZE_GPU_BYTES   = static_cast<size_t>(RD::Renderer_Buffer::Count) * sizeof(uint64_t);
