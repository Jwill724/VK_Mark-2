#pragma once

#include <functional>

// General Engine Limits
constexpr uint32_t MAX_DRAWS = 16384u;
//constexpr uint32_t MAX_DRAWS = 65536u;
constexpr uint32_t MAX_VISIBLE_TRANSFORMS = MAX_DRAWS;
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3u;

// Default spawn with loading
constexpr glm::vec3 SPAWNPOINT(1.0f, 1.0f, 1.0f);

constexpr uint32_t MAX_THREADS = 12u;
constexpr uint32_t MAX_PUSH_CONSTANT_SIZE = 256u;
constexpr float TARGET_FRAME_RATE_60 = 59.94f;
constexpr float TARGET_FRAME_RATE_120 = 119.88f;
constexpr float TARGET_FRAME_RATE_144 = 143.856f;
constexpr float TARGET_FRAME_RATE_240 = 239.76f;

// Resource Limits
constexpr float ANISOTROPY_LEVEL_16 = 16.0f;
constexpr float ANISOTROPY_LEVEL_8 = 8.0f;
constexpr float ANISOTROPY_LEVEL_4 = 4.0f;
static constexpr uint32_t MSAACOUNT_8 = 8u;
static constexpr uint32_t MSAACOUNT_4 = 4u;
static constexpr uint32_t MSAACOUNT_2 = 2u;
constexpr uint32_t MAX_MIP_LEVELS = 12u;
constexpr uint32_t MAX_ENV_SETS = 16u; // 256 uniform alignment 16 * ivec4(16 bytes)
constexpr uint32_t MAX_SHADOW_CASCADES = 4u;
constexpr uint32_t KERNEL_BLOCK_SIZE = 128u; // ssao kernel
constexpr uint32_t MAX_LUMINANCE_GROUPS = 65536u;
constexpr uint32_t DEPTH_PYRAMID_MIP_COUNT = 5u;

// Descriptor info
constexpr uint32_t GLOBAL_SET = 0u;
constexpr uint32_t FRAME_SET = 1u;
constexpr uint32_t PUSH_SET = 2u;

// Shared between global and frame
constexpr uint32_t ADDRESS_TABLE_BINDING = 0u;

// Global bindings
constexpr uint32_t GLOBAL_BINDING_ENV_INDEX = 1u;
constexpr uint32_t GLOBAL_BINDING_SSAO_KERNEL = 2u;
constexpr uint32_t GLOBAL_BINDING_DEBUG_INLINE = 3u;
constexpr uint32_t GLOBAL_BINDING_SAMPLER_CUBE = 4u;
constexpr uint32_t GLOBAL_BINDING_STORAGE_IMAGE = 5u;
constexpr uint32_t GLOBAL_BINDING_COMBINED_SAMPLER = 6u;

// Frame bindings
constexpr uint32_t FRAME_BINDING_SCENE = 1u;
constexpr uint32_t FRAME_BINDING_CSM = 2u;

// Push bindings
constexpr uint32_t PUSH_BINDING_DEPTH_TEX = 0u;
constexpr uint32_t PUSH_BINDING_NORMAL_TEX = 1u;
constexpr uint32_t PUSH_BINDING_NOISE_TEX = 2u;
constexpr uint32_t PUSH_BINDING_OUTPUT_1_TEX = 3u;
constexpr uint32_t PUSH_BINDING_OUTPUT_2_TEX = 4u;
constexpr uint32_t PUSH_BINDING_OUTPUT_3_TEX = 5u;
constexpr uint32_t PUSH_BINDING_INPUT_1_TEX = 6u;
constexpr uint32_t PUSH_BINDING_INPUT_2_TEX = 7u;
constexpr uint32_t PUSH_BINDING_INPUT_3_TEX = 8u;

// Image array sizes
constexpr uint32_t MAX_SAMPLER_CUBE_IMAGES = 100u;
constexpr uint32_t MAX_STORAGE_IMAGES = 100u;
constexpr uint32_t MAX_COMBINED_SAMPLERS_IMAGES = 10000u;

// Threading / Job System
constexpr uint32_t JOB_WORKER_COUNT = MAX_THREADS;

// TODO:
// This will work, i just to fix the compute queue syncing,
// add gpu sorting, fix the visible count and visiblemeshIds read and write buffer shit
// draws will have to be fully built on gpu for this to properly work
// gpu accel is fucking busted
const static bool GPU_ACCELERATION_ENABLED = false;