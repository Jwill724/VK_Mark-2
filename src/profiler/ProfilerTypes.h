#pragma once

#include "renderer/RendererDefinitions.h"
#include <string>
#include <array>

namespace RD = RendererDefinitions;

class TimerAverager
{
public:
	void Add(float sample, float alpha = 0.1f)
	{
		if (!m_initialized)
		{
			m_smoothed = sample;
			m_initialized = true;
			return;
		}

		m_smoothed = (1.0f - alpha) * m_smoothed + alpha * sample;
	}

	float Get() const { return m_smoothed; }

	bool IsInitialized() const noexcept { return m_initialized == true; }

private:
	float m_smoothed = 0.0f;
	bool m_initialized = false;
};

struct VRAMStats
{
	VkDeviceSize used = 0;       // live VMA allocations
	VkDeviceSize committed = 0;  // VMA VkDeviceMemory blocks
	VkDeviceSize driverUsed = 0; // VK_EXT_memory_budget estimate
	VkDeviceSize budget = 0;
};

struct FrameStats
{
	std::string gpuName;

	float deltaSecondsRaw = 1.0f / 60.0f;
	float frameTimeRawMs = 0.0f;
	float gpuFrameTimeRawMs = 0.0f;

	TimerAverager deltaTime;
	TimerAverager fps;
	TimerAverager frameTime;
	TimerAverager frameTimeRaw;
	TimerAverager gpuFrameTime{};
	TimerAverager sceneUpdateTime;
	TimerAverager drawTime;

	VRAMStats vramStats{};

	bool capFramerate = true;
	float targetFrameRate = RD::TARGET_FPS_60;
};

struct TotalAssetDataCounts
{
	uint32_t totalVertexCount = 0u;
	uint32_t totalIndexCount = 0u;
	uint32_t totalMaterialCount = 0u;
	uint32_t totalMeshCount = 0u;

	void Clear() noexcept
	{
		totalVertexCount = 0u;
		totalIndexCount = 0u;
		totalMaterialCount = 0u;
		totalMeshCount = 0u;
	}
};

struct PassTimingStats
{
	std::string name;

	bool activeThisFrame = false;
	bool activeLastFrame = false;

	bool asyncQueueThisFrame = false;
	bool asyncQueueLastFrame = false;

	float cpuMsRaw = 0.0f;
	float gpuMsRaw = 0.0f;

	TimerAverager cpuMsAverage;
	TimerAverager gpuMsAverage;
};

struct AsyncComputeStats
{
	bool bDedicatedQueue = false;

	bool bActiveThisFrame = false;

	uint32_t graphicsBatchCount = 1u;   // 1 = single submit, 3 = split
	uint32_t asyncPassCount     = 0u;   // C0
	uint32_t overlapPassCount   = 0u;   // G1, the passes beside C0
};
