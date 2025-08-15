#pragma once

#include "core/AssetManager.h"
#include "core/ResourceManager.h"
#include "renderer/frame/FrameContext.h"
#include "engine/platform/input/Camera.h"

using namespace FrameContext;

// Holds and controls scene data
namespace RenderScene {
	GPUSceneData& getCurrentSceneData();

	inline std::unordered_map<std::string, std::shared_ptr<ModelAsset>> _loadedScenes;

	extern Camera _mainCamera;

	void setScene();
	void updateCamera();
	void copyFrustumToFrame(CullingPushConstantsAddrs& frustumData);

	void transformSceneNodes();

	void updateVisiblesInstances(FrameCtx& frameCtx);

	void allocateSceneBuffer(FrameCtx& frameCtx, const VmaAllocator allocator);
	void updateScene(FrameCtx& frameCtx, GPUResources& resources);
	void renderGeometry(FrameCtx& frameCtx, Profiler& profiler);
	void drawIndirectCommands(FrameCtx& frameCtx, GPUResources& resources, Profiler& profiler);
}