#pragma once

#include "common/ResourceTypes.h"
#include "renderer/gpu/PipelineManager.h"

struct IndirectStats {
	uint32_t commands = 0;
	uint32_t subdraws = 0;
};

struct VRAMStats {
	VkDeviceSize used = 0;
	VkDeviceSize budget = 0;
};

struct TimerAverager {
	float smoothed = 0.0f;
	bool initialized = false;

	void add(float sample, float alpha = 0.1f) {
		if (!initialized) {
			smoothed = sample;
			initialized = true;
			return;
		}

		smoothed = (1.0f - alpha) * smoothed + alpha * sample;
	}

	float get() const {
		return smoothed;
	}
};

struct FrameStats {
	std::string gpuName;

	uint64_t triangleCount = 0;

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
	float targetFrameRate = TARGET_FPS_60;

	uint32_t directDraws = 0;
	IndirectStats opaqueIndirect;
	IndirectStats transparentIndirect;
	IndirectStats directionalCSMIndirect;
	IndirectStats flashlightShadowIndirect;
};

struct PipelineOverride {
	bool enabled = false;
	PipelineID selectedID = PipelineID::Wireframe;
};

struct alignas(4) DebugToggles {
	uint32_t enableOBBs = 0;
	uint32_t enableProfilerView = 0;
	uint32_t enableSettings = 1;
	uint32_t aaMode = AA_CMAA2;

	uint32_t aoMode = AO_GTAO;
	uint32_t enableShadows = 1;
	uint32_t enableVolumetrics = 1;
	uint32_t activeEnvMap = 0;

	uint32_t meshCount = 0;
	uint32_t materialCount = 0;
	uint32_t transformCount = 0;
	uint32_t vertexCount = 0;

	uint32_t indexCount = 0;
	uint32_t enableLensFlare = 1;
	uint32_t enableChromaticAberration = 1;
	uint32_t enableSSS = 1;

	uint32_t showAlbedo = 0;
	uint32_t showNormals = 0;
	uint32_t showRoughness = 0;
	uint32_t showMetallic = 0;

	uint32_t showAmbientOcclusion = 0;
	uint32_t showSpecular = 0;
	uint32_t showDiffuse = 0;
	uint32_t tonemapper = TM_ACESFILM;

	uint32_t showEmissive = 0;
	uint32_t showBentNormals = 0;
	uint32_t showCascadeSplits = 0;
	uint32_t showSSS = 0;

	uint32_t shadowFilter = SHADOW_FILTER_PCF;
	uint32_t pad0;
	uint32_t pad1;
	uint32_t pad2;
};

struct PassTimingStats {
	bool activeThisFrame = false;
	bool activeLastFrame = false;

	float cpuMsRaw = 0.0f;
	float gpuMsRaw = 0.0f;

	TimerAverager cpuMsAverage;
	TimerAverager gpuMsAverage;
};

inline uint64_t trianglesFromNonIndexed(
	VkPrimitiveTopology topology,
	uint64_t vertexCount)
{
	switch (topology) {
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
		return vertexCount / 3u;

	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
		return vertexCount >= 3u ? (vertexCount - 2u) : 0u;

	default:
		return 0u;
	}
}

inline uint64_t trianglesFromIndexed(
	VkPrimitiveTopology topology,
	uint32_t indexCount,
	uint32_t instanceCount)
{
	uint64_t baseTriangleCount = 0;

	switch (topology) {
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
		baseTriangleCount = static_cast<uint64_t>(indexCount / 3u);
		break;

	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
	case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
		baseTriangleCount = indexCount >= 3u
			? static_cast<uint64_t>(indexCount - 2u)
			: 0u;
		break;

	default:
		return 0u;
	}

	return baseTriangleCount * static_cast<uint64_t>(instanceCount);
}

inline uint64_t sumTrianglesIndirectRange(
	const std::vector<VkDrawIndexedIndirectCommand>& drawCommands,
	uint32_t firstCommand,
	uint32_t commandCount,
	VkPrimitiveTopology topology)
{
	uint64_t totalTriangles = 0;
	const size_t baseIndex = static_cast<size_t>(firstCommand);

	for (uint32_t commandIndex = 0; commandIndex < commandCount; ++commandIndex) {
		const auto& drawCommand = drawCommands[baseIndex + static_cast<size_t>(commandIndex)];

		totalTriangles += trianglesFromIndexed(
			topology,
			drawCommand.indexCount,
			drawCommand.instanceCount
		);
	}

	return totalTriangles;
}
