#pragma once

#include "core/AssetManager.h"
#include "Renderer.h"
#include "renderer/gpu_types/PipelineManager.h"
#include "input/Camera.h"
#include "common/ResourceTypes.h"
#include "common/EngineTypes.h"

constexpr glm::vec3 SPAWNPOINT(-1, 1, -1);
//constexpr glm::vec3 SPAWNPOINT(50, 1, -1);

// Holds and controls scene data
namespace RenderScene {
	GPUSceneData& getCurrentSceneData();

	inline std::unordered_map<std::string, std::shared_ptr<ModelAsset>> _loadedScenes;

	extern Camera _mainCamera;

	void setScene();
	void updateCamera();
	void copyFrustumToFrame(CullingPushConstantsAddrs& frustumData);

	void transformSceneNodes();

	void updateVisiblesInstances(FrameContext& frameCtx);

	void allocateSceneBuffer(FrameContext& frameCtx, const VmaAllocator allocator);
	void updateScene(FrameContext& frameCtx, GPUResources& resources);
	void renderGeometry(FrameContext& frameCtx, Profiler& profiler);
	void drawIndirectCommands(FrameContext& frameCtx, GPUResources& resources, Profiler& profiler);
}