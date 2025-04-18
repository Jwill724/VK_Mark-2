#pragma once

#include "vulkan/PipelineManager.h"

// Centralized control / config hub(rendering toggles, reloads, pause, etc);

struct EngineStats {
	float frametime;
	int triangleCount;
	int drawcallCount;
	float sceneUpdateTime;
	float meshDrawTime;
};

GraphicsPipeline* getPipelineByType(PipelineType type);

struct PipelineOverride {
	bool enabled = false;
	PipelineType selected = PipelineType::Wireframe;
};

struct RenderSceneSettings {
	static inline bool drawBoundingBoxes = false;
};

inline PipelineOverride pipelineOverride;