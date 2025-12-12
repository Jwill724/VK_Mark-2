#pragma once

#include "common/ResourceTypes.h"
#include "renderer/gpu/PipelineManager.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

struct IndirectStats {
	uint32_t commands{ 0 };
	uint32_t subdraws{ 0 };
};

struct VRAMStats {
	VkDeviceSize used = 0;
	VkDeviceSize budget = 0;
};

// Exponential moving average, to show clearer values over frames
struct TimerAverager {
	float smoothed = 0.0f;
	bool initialized = false;

	void add(float sample, float alpha = 0.1f) {
		if (!initialized) {
			smoothed = sample;
			initialized = true;
		}
		else {
			smoothed = (1.0f - alpha) * smoothed + alpha * sample;
		}
	}

	float get() const { return smoothed; }
};

struct FrameStats {
	std::string gpuName;
	uint64_t triangleCount = 0;

	TimerAverager deltaTime;
	TimerAverager fps;
	TimerAverager frameTime;
	TimerAverager sceneUpdateTime;
	TimerAverager drawTime;

	VRAMStats vramStats{};

	bool capFramerate = false;
	float targetFrameRate = 0.0f;

	uint32_t directDraws = 0;
	IndirectStats opaqueIndirect;
	IndirectStats transparentIndirect;
};

struct PipelineOverride {
	bool enabled = false;
	PipelineID selectedID = PipelineID::Wireframe;
};

// inline uniform block in global set 0
struct alignas(4) DebugToggles {
	// Higher level toggles
	uint32_t enableOBBs = 0;
	uint32_t enableCascadeVPs = 0;
	uint32_t enableSettings = 1;
	uint32_t enableStats = 1;

	uint32_t aoMode = AO_GTAO;
	uint32_t enableShadows = 1;
	uint32_t enableVolumetrics = 1;
	uint32_t activeEnvMap = 0; // Indexes into an array

	// draw stats
	uint32_t meshCount = 0;
	uint32_t materialCount = 0;
	uint32_t transformCount = 0;
	uint32_t vertexCount = 0;

	uint32_t indexCount = 0;
	uint32_t enableLensFlare = 0;
	uint32_t enableChromaticAberration = 0;
	uint32_t pad0 = 0;

	// fragment shader outputs
	uint32_t showAlbedo = 0;
	uint32_t showNormals = 0;
	uint32_t showRoughness = 0;
	uint32_t showMetallic = 0;

	uint32_t showAmbientOcclusion = 0;
	uint32_t showSpecular = 0;
	uint32_t showDiffuse = 0;
	uint32_t showCascadeSplits = 0;

	uint32_t showEmissive = 0;
	uint32_t showBakedAO = 0;
	uint32_t pad1{};
};


class Profiler {
public:
	void beginFrame();
	void endFrame();

	bool rendererWasStalled{ false };

	void startTimer();
	float endTimerMS() const;
	inline float endTimerSec() const {
		return endTimerMS() / 1000.0f;
	}

	bool assetsLoaded{ false };

	FrameStats& getStats() { return _stats; }

	void resetDrawCalls() {
		_stats.triangleCount = 0u;
		_stats.directDraws = 0u;
		_stats.opaqueIndirect.commands = 0u;
		_stats.opaqueIndirect.subdraws = 0u;
		_stats.transparentIndirect.commands = 0u;
		_stats.transparentIndirect.subdraws = 0u;
	}

	inline void addDirect(uint32_t calls, uint64_t tris = 0) {
		_stats.directDraws += calls;
		_stats.triangleCount += tris;
	}

	// Support both material pass views

	inline void addOpaqueIndirect(uint32_t commands, uint32_t subdraws, uint64_t tris = 0) {
		_stats.opaqueIndirect.commands += commands;
		_stats.opaqueIndirect.subdraws += subdraws;
		_stats.triangleCount += tris;
	}
	inline void addTransparentIndirect(uint32_t commands, uint32_t subdraws, uint64_t tris = 0) {
		_stats.transparentIndirect.commands += commands;
		_stats.transparentIndirect.subdraws += subdraws;
		_stats.triangleCount += tris;
	}

	glm::vec3 cameraPos{};
	std::mutex camMutex;

	DebugToggles debugToggles;
	PipelineOverride pipeOverride;
	SSAOPush ssaoSettings;
	GTAOPush gtaoSettings;
	VolumetricPush volLightSettings;

	VRAMStats GetTotalVRAMUsage(VkPhysicalDevice device, VmaAllocator allocator);

	void enablePlatformTimerPrecision();
	void disablePlatformTimerPrecision();

	Profiler();
	~Profiler();
private:
	FrameStats _stats;

	LARGE_INTEGER _qpcFreq{};
	long long _qpcFreqLL = 0;
	long long _periodLL = 0; // ticks per frame
	long long _nextTickLL = 0; // next target tick
	double _qpcInv = 0.0; // seconds per tick
	double _frameStartTime = 0.0;
	double _lastDeltaTime = 0.0;
	LARGE_INTEGER _startTimer{};
};

inline uint64_t trianglesFromNonIndexed(VkPrimitiveTopology topo, uint64_t vertexCount) {
	switch (topo) {
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:  return vertexCount / 3u;
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: return vertexCount >= 3 ? (vertexCount - 2u) : 0u;
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:   return vertexCount >= 3 ? (vertexCount - 2u) : 0u;
	default:                                   return 0u; // points/lines/patches -> no triangles
	}
}

inline uint64_t trianglesFromIndexed(VkPrimitiveTopology topo,
	uint32_t indexCount,
	uint32_t instanceCount)
{
	uint64_t base = 0;
	switch (topo) {
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
		static_cast<uint64_t>(base = indexCount / 3u);
		break;
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
		base = static_cast<uint64_t>(indexCount >= 3u) ? static_cast<uint64_t>(indexCount - 2u) : 0u; break;
	default:
		return 0; // points/lines/patches -> no triangles
	}
	return base * static_cast<uint64_t>(instanceCount);
}


inline uint64_t sumTrianglesIndirectRange(const std::vector<VkDrawIndexedIndirectCommand>& cmds,
	uint32_t first,
	uint32_t count,
	VkPrimitiveTopology topo)
{
	uint64_t total = 0;
	const size_t base = first;
	for (uint32_t i = 0; i < count; ++i) {
		const auto& d = cmds[base + i];
		total += trianglesFromIndexed(topo, d.indexCount, d.instanceCount);
	}
	return total;
}