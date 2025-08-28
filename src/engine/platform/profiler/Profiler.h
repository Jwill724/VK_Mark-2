#pragma once

#include "common/ResourceTypes.h"
#include "renderer/gpu/PipelineManager.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

struct IndirectStats {
	std::atomic<uint32_t> commands{ 0 };
	std::atomic<uint32_t> subdraws{ 0 };
};


struct FrameStats {
	std::atomic<uint64_t> triangleCount = 0;
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

	std::atomic<uint32_t> directDraws{ 0 };
	IndirectStats opaqueIndirect;
	IndirectStats transparentIndirect;
};
struct PipelineOverride {
	bool enabled = false;
	PipelineID selectedID = PipelineID::Wireframe;
};

struct DebugToggles {
	bool showOBBs = false;
	bool enableSettings = false;
	bool enableStats = true;
	//bool showNormals = false;
	//bool showAlbedo = false;
	//bool showEmissive = false;
	//bool showAO = false;
	//bool showSpecular = false;
	//bool showDiffuse = false;
	//bool showMetallic = false;
	//bool showRoughness = false;
	bool showWireframe = false;
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
		_stats.triangleCount.store(0);
		_stats.directDraws.store(0);
		_stats.opaqueIndirect.commands.store(0);
		_stats.opaqueIndirect.subdraws.store(0);
		_stats.transparentIndirect.commands.store(0);
		_stats.transparentIndirect.subdraws.store(0);
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

	VkDeviceSize GetTotalVRAMUsage(VkPhysicalDevice device, VmaAllocator allocator);

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