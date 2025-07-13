#pragma once

#include <functional>

// General Engine Limits
constexpr uint32_t MAX_OPAQUE_DRAWS = 65536;
constexpr uint32_t MAX_TRANSPARENT_DRAWS = 16384;
constexpr uint32_t MAX_VISIBLE_TRANSFORMS = MAX_OPAQUE_DRAWS + MAX_TRANSPARENT_DRAWS;


// Compute work sizes
static constexpr uint32_t LOCAL_SIZE_X = 64;

constexpr unsigned int MAX_FRAMES_IN_FLIGHT = 2;
constexpr unsigned int MAX_THREADS = 12;
constexpr uint32_t MAX_PUSH_CONSTANT_SIZE = 256;
constexpr float TARGET_FRAME_RATE_60 = 60.0f;
constexpr float TARGET_FRAME_RATE_120 = 120.0f;
constexpr float TARGET_FRAME_RATE_144 = 144.0f;
constexpr float TARGET_FRAME_RATE_240 = 240.0f;

// Image / Texture Limits
constexpr float ANISOTROPY_LEVEL = 16.0f;
static constexpr uint32_t MSAACOUNT_8 = 8;
static constexpr uint32_t MSAACOUNT_4 = 4;
static constexpr uint32_t MSAACOUNT_2 = 2;
constexpr uint32_t MAX_MIP_LEVELS = 12;
constexpr uint32_t MAX_ENV_SETS = 16; // 256 uniform alignment 16 * vec4(16 bytes)

// Descriptor max sizing
constexpr uint32_t MAX_SAMPLER_CUBE_IMAGES = 100;
constexpr uint32_t MAX_STORAGE_IMAGES = 100;
constexpr uint32_t MAX_COMBINED_SAMPLERS_IMAGES = 1000;

// Command Buffer / Pool Limits
constexpr uint32_t TOTAL_CMD_BUFFERS = MAX_THREADS * MAX_FRAMES_IN_FLIGHT;

// Threading / Job System
constexpr uint32_t JOB_WORKER_COUNT = MAX_THREADS;

// TODO:
// This will work, i just to fix the compute queue syncing,
// add gpu sorting, fix the visible count and visiblemeshIds read and write buffer shit
// draws will have to be fully built on gpu for this to properly work
// gpu accel is fucking busted
const static bool GPU_ACCELERATION_ENABLED = false;