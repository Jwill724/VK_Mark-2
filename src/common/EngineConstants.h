#pragma once

#include <functional>

// General Engine Limits
constexpr unsigned int MAX_FRAMES_IN_FLIGHT = 2;
constexpr unsigned int MAX_THREADS = 12;
constexpr uint32_t MAX_PUSH_CONSTANT_SIZE = 256;
constexpr float TARGET_FRAME_RATE_60 = 60.f;
constexpr float TARGET_FRAME_RATE_120 = 120.f;
constexpr float TARGET_FRAME_RATE_144 = 144.f;
constexpr float TARGET_FRAME_RATE_240 = 240.f;

// Image / Texture Limits
constexpr float ANISOTROPY_LEVEL = 16.f;
constexpr static uint32_t MSAACOUNT = 8;
constexpr uint32_t MAX_MIP_LEVELS = 12;

// Descriptor max sizing
constexpr uint32_t MAX_SAMPLER_CUBE_IMAGES = 100;
constexpr uint32_t MAX_STORAGE_IMAGES = 100;
constexpr uint32_t MAX_COMBINED_SAMPLERS_IMAGES = 1000;

// Command Buffer / Pool Limits
constexpr uint32_t TOTAL_CMD_BUFFERS = MAX_THREADS * MAX_FRAMES_IN_FLIGHT;

// Threading / Job System
constexpr uint32_t JOB_WORKER_COUNT = MAX_THREADS;