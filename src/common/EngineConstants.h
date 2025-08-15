#pragma once

#include <functional>

// General Engine Limits
constexpr uint32_t MAX_OPAQUE_DRAWS = 65536;
constexpr uint32_t MAX_TRANSPARENT_DRAWS = 16384;
constexpr uint32_t MAX_VISIBLE_TRANSFORMS = MAX_OPAQUE_DRAWS + MAX_TRANSPARENT_DRAWS;
constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

// Default spawn with loading
constexpr glm::vec3 SPAWNPOINT(1, 1, 1);

static constexpr uint32_t LOCAL_SIZE_X = 64;
constexpr unsigned int MAX_THREADS = 12;
constexpr uint32_t MAX_PUSH_CONSTANT_SIZE = 256;
constexpr float TARGET_FRAME_RATE_60 = 59.94f;
constexpr float TARGET_FRAME_RATE_120 = 119.88f;
constexpr float TARGET_FRAME_RATE_144 = 143.856f;
constexpr float TARGET_FRAME_RATE_240 = 239.76f;

// Image / Texture Limits
constexpr float ANISOTROPY_LEVEL = 16.0f;
static constexpr uint32_t MSAACOUNT_8 = 8;
static constexpr uint32_t MSAACOUNT_4 = 4;
static constexpr uint32_t MSAACOUNT_2 = 2;
constexpr uint32_t MAX_MIP_LEVELS = 12;
constexpr uint32_t MAX_ENV_SETS = 16; // 256 uniform alignment 16 * uvec4(16 bytes)
constexpr uint32_t MAX_MATERIALS = 512;

// Descriptor info
constexpr uint32_t GLOBAL_SET = 0;
constexpr uint32_t FRAME_SET = 1;
constexpr uint32_t ADDRESS_TABLE_BINDING = 0;
constexpr uint32_t GLOBAL_BINDING_ENV_INDEX = 1;
constexpr uint32_t GLOBAL_BINDING_SAMPLER_CUBE = 2;
constexpr uint32_t GLOBAL_BINDING_STORAGE_IMAGE = 3;
constexpr uint32_t GLOBAL_BINDING_COMBINED_SAMPLER = 4;
constexpr uint32_t FRAME_BINDING_SCENE = 1;
constexpr uint32_t MAX_SAMPLER_CUBE_IMAGES = 100;
constexpr uint32_t MAX_STORAGE_IMAGES = 100;
constexpr uint32_t MAX_COMBINED_SAMPLERS_IMAGES = 1000;

// Threading / Job System
constexpr uint32_t JOB_WORKER_COUNT = MAX_THREADS;

// TODO:
// This will work, i just to fix the compute queue syncing,
// add gpu sorting, fix the visible count and visiblemeshIds read and write buffer shit
// draws will have to be fully built on gpu for this to properly work
// gpu accel is fucking busted
const static bool GPU_ACCELERATION_ENABLED = false;