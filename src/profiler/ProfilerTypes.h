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
	uint64_t used = 0;
	uint64_t budget = 0;
};

struct FrameStats
{
	std::string gpuName;

	float deltaSecondsRaw = 1.0f / 60.0f;
	float vramQueryTimerSeconds = 0.0f;
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

	float cpuMsRaw = 0.0f;
	float gpuMsRaw = 0.0f;

	TimerAverager cpuMsAverage;
	TimerAverager gpuMsAverage;
};
