#pragma once

#include <functional>

// General Engine Limits
constexpr uint32_t MAX_FRAME_INSTANCES_TOTAL     = 262144u;
constexpr uint32_t MAX_FRAME_DRAW_COMMANDS_TOTAL = 65536u;
constexpr uint32_t MAX_INSTANCE_TRANSFORMS = 200000u;
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3u;
constexpr uint32_t MAX_LIGHTS = 4096u; // standard
//constexpr uint32_t MAX_LIGHTS = 8192u; // high
//constexpr uint32_t MAX_LIGHTS = 10240u; // stress
constexpr uint32_t LIGHT_FLAG_CASTS_SPOT_SHADOW = 1u << 0;
constexpr uint32_t LIGHT_FLAG_FLASHLIGHT        = 1u << 1;
constexpr uint32_t LIGHT_FLAG_FLASHLIGHT_OFF    = 1u << 2;

// Static lights in global list
constexpr uint32_t LIGHT_LIST_STATIC_COUNT = 1u;
constexpr uint32_t LIGHT_LIST_SLOT_FLASHLIGHT = 0u;

// Default spawn with loading
constexpr glm::vec3 SPAWNPOINT(1.0f, 1.0f, 1.0f);

constexpr uint32_t MAX_THREADS             = 12u;
constexpr uint32_t MAX_PUSH_CONSTANT_SIZE  = 256u;
constexpr float TARGET_FPS_60       = 60.0f;
constexpr float TARGET_FPS_90       = 90.0f;
constexpr float TARGET_FPS_100      = 100.0f;
constexpr float TARGET_FPS_120      = 120.0f;
constexpr float TARGET_FPS_144      = 144.0f;
constexpr float TARGET_FPS_165      = 165.0f;
constexpr float TARGET_FPS_180      = 180.0f;
constexpr float TARGET_FPS_200      = 200.0f;
constexpr float TARGET_FPS_240      = 240.0f;
constexpr float TARGET_FPS_300      = 300.0f;
constexpr float TARGET_FPS_360      = 360.0f;
constexpr float TARGET_FPS_480      = 480.0f;

// Resource Limits
constexpr float ANISOTROPY_LEVEL_16        = 16.0f;
constexpr float ANISOTROPY_LEVEL_8         = 8.0f;
constexpr float ANISOTROPY_LEVEL_4         = 4.0f;
constexpr uint32_t MAX_MIP_LEVELS          = 12u;
constexpr uint32_t MAX_ENV_SETS            = 8u;  // 128 uniform alignment
constexpr uint32_t MAX_SHADOW_CASCADES     = 4u;
constexpr uint32_t MAX_LUMINANCE_GROUPS    = 65536u;
constexpr uint32_t HI_Z_MIP_COUNT = 5u;

// Descriptor info
constexpr uint32_t GLOBAL_SET = 0u;
constexpr uint32_t FRAME_SET  = 1u;
constexpr uint32_t PUSH_SET   = 2u;

const uint32_t INDIRECT_DISPATCH_SLOT_LIGHTS         = 0u;  // args[0]
const uint32_t INDIRECT_DISPATCH_SLOT_CLUSTERS       = 1u;  // args[1]
const uint32_t INDIRECT_DISPATCH_SLOT_CMAA2_SHAPES   = 2u;  // args[2]
const uint32_t INDIRECT_DISPATCH_SLOT_CMAA2_DEFERRED = 3u;  // args[3]

// Shared between global and frame
constexpr uint32_t ADDRESS_TABLE_BINDING = 0u;

// Global bindings
constexpr uint32_t GLOBAL_BINDING_ENV_INDEX        = 1u;
constexpr uint32_t GLOBAL_BINDING_DEBUG_INLINE     = 2u;
constexpr uint32_t GLOBAL_BINDING_SAMPLER_CUBE     = 3u;
constexpr uint32_t GLOBAL_BINDING_COMBINED_SAMPLER = 4u;

// Frame bindings
constexpr uint32_t FRAME_BINDING_SCENE     = 1u;
constexpr uint32_t FRAME_BINDING_CSM       = 2u;
constexpr uint32_t FRAME_BINDING_CLUSTERED = 3u;

// Push bindings
constexpr uint32_t PUSH_BINDING_INPUT_1_TEX  = 0u;
constexpr uint32_t PUSH_BINDING_INPUT_2_TEX  = 1u;
constexpr uint32_t PUSH_BINDING_INPUT_3_TEX  = 2u;
constexpr uint32_t PUSH_BINDING_INPUT_4_TEX  = 3u;
constexpr uint32_t PUSH_BINDING_INPUT_5_TEX  = 4u;
constexpr uint32_t PUSH_BINDING_INPUT_6_TEX  = 5u;
constexpr uint32_t PUSH_BINDING_INPUT_7_TEX  = 6u;
constexpr uint32_t PUSH_BINDING_INPUT_8_TEX  = 7u;
constexpr uint32_t PUSH_BINDING_INPUT_9_TEX  = 8u;
constexpr uint32_t PUSH_BINDING_OUTPUT_1_TEX = 9u;
constexpr uint32_t PUSH_BINDING_OUTPUT_2_TEX = 10u;
constexpr uint32_t PUSH_BINDING_OUTPUT_3_TEX = 11u;
constexpr uint32_t PUSH_BINDING_OUTPUT_4_TEX = 12u;
constexpr uint32_t PUSH_BINDING_OUTPUT_5_TEX = 13u;

// Image array sizes
constexpr uint32_t MAX_SAMPLER_CUBE_IMAGES      = 100u;
constexpr uint32_t MAX_COMBINED_SAMPLERS_IMAGES = 10000u;

// Threading / Job System
constexpr uint32_t JOB_WORKER_COUNT = MAX_THREADS;

// GPU material flags
constexpr uint32_t MATERIAL_FLAG_ALPHA_MASKED  = 1u << 0;
constexpr uint32_t MATERIAL_FLAG_CASTS_SHADOWS = 1u << 1;
constexpr uint32_t MATERIAL_FLAG_HAS_NORMAL_MAP = 1u << 2;
