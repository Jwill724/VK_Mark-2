#pragma once

#include "core/AssetManager.h"
#include "Renderer.h"
#include "renderer/gpu/PipelineManager.h"
#include "input/Camera.h"
#include "common/ResourceTypes.h"
#include "common/EngineTypes.h"

//constexpr glm::vec3 SPAWNPOINT(1, 1, -1);
constexpr glm::vec3 SPAWNPOINT(50, 1, -1);

// Holds and controls scene data
namespace RenderScene {
	GPUSceneData& getCurrentSceneData();

	inline std::unordered_map<std::string, std::shared_ptr<LoadedGLTF>> _loadedScenes;

	extern Camera _mainCamera;

	extern DrawContext _mainDrawContext;

	void setScene();
	void updateCamera();
	void updateFrustum();

	void updateTransforms();
	void updateSceneGraph();

	void drawBatches(const FrameContext& frameCtx,
		const std::vector<SortedBatch>& opaqueBatches,
		const std::vector<RenderObject>& transparentObjects,
		GPUResources& resources);

	void allocateSceneBuffer(FrameContext& frameCtx, const VmaAllocator allocator);
	void updateScene(FrameContext& frameCtx, GPUResources& resources);
	void renderGeometry(FrameContext& frameCtx);
}