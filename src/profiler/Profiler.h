#pragma once

#include "common/Vk_Types.h"
#include "common/ResourceTypes.h"
#include "renderer/gpu_types/PipelineManager.h"
#include "common/EngineConstants.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

struct FrameStats {
	std::atomic<uint32_t> drawCalls = 0;
	std::atomic<uint32_t> triangleCount = 0;
	std::atomic<float> deltaTime = 0.0f;
	std::atomic<float> frameTime = 0.0f;
	std::atomic<float> fps = 0.0f;
	std::atomic<float> sceneUpdateTime = 0.0f;
	std::atomic<float> drawTime = 0.0f;

	std::atomic<size_t> vramUsed = 0;

	// V-sync is default present mode for now
	// frame capping is fucking busted
	bool capFramerate = false;
	float targetFrameRate = 0.0f;
};
struct PipelineOverride {
	bool enabled = false;
	PipelineType selected = PipelineType::Wireframe;
};

struct DebugToggles {
	bool showAABBs = false;
	bool showNormals = false;
	bool showSpecular = false;
	bool showDiffuse = false;
	bool showMetallic = false;
	bool showRoughness = false;
	bool forceWireframe = false;
};

class Profiler {
public:
	void beginFrame();
	void endFrame();

	void startTimer();
	float endTimer() const;

	bool assetsLoaded = false;

	FrameStats& getStats() { return _stats; }

	void resetDrawCalls() {
		_stats.drawCalls.store(0);
		_stats.triangleCount.store(0);
	}

	void addDrawCall(uint32_t tris) {
		_stats.drawCalls++;
		_stats.triangleCount += tris;
	}

	void resetRenderTimers() {
		_stats.drawTime.store(0);
		_stats.sceneUpdateTime.store(0);
		_stats.frameTime.store(0);
		_stats.fps.store(0);
	}

	glm::vec3 cameraPos{};
	std::mutex camMutex;

	DebugToggles debugToggles;
	PipelineOverride pipeOverride;
	VkPipeline getPipelineByType(PipelineType type) const;

	VkDeviceSize GetTotalVRAMUsage(VkPhysicalDevice device, VmaAllocator allocator);

	void enablePlatformTimerPrecision();
	void disablePlatformTimerPrecision();

	~Profiler();

private:
	FrameStats _stats;

	LARGE_INTEGER _qpcFreq{};
	double _frameStartTime = 0.0;
	double _lastDeltaTime = 0.0;
	LARGE_INTEGER _startTimer{};
};