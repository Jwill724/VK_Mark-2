#pragma once

#include "core/AssetManager.h"
#include "core/ResourceManager.h"
#include "renderer/frame/FrameContext.h"
#include "engine/platform/input/Camera.h"

// Holds and controls scene data
namespace RenderScene {
	GPUSceneData& getCurrentSceneData();

	inline std::unordered_map<SceneID, std::shared_ptr<ModelAsset>> _loadedScenes;

	inline static std::unordered_map<SceneID, SceneProfileEntry> _sceneProfiles {
		{ SceneID::Sponza, { "Sponza", DrawType::DrawStatic, 1, 1, 1 } },
		{ SceneID::MRSpheres, { "MRSpheres", DrawType::DrawStatic, 1, 1, 1 } },
		{ SceneID::Cube, { "Cube", DrawType::DrawMultiDynamic, 50, 0, 0 } },
		{ SceneID::DamagedHelmet, { "DamagedHelmet", DrawType::DrawMultiStatic, 100, 0, 0 } },
		{ SceneID::DragonAttenuation, { "Dragon", DrawType::DrawStatic, 1, 1, 1 } }
	};

	extern std::vector<GlobalInstance> _globalInstances;
	extern std::vector<glm::mat4> _globalTransforms;

	const Camera getCamera();

	void setScene();
	void updateCamera();
	void copyFrustumToFrame(CullingPushConstantsAddrs& frustumData);

	void cleanScene();

	void allocateSceneBuffer(FrameContext& frameCtx, const VmaAllocator allocator);
	void updateScene(FrameContext& frameCtx, GPUResources& resources);
	void renderGeometry(FrameContext& frameCtx, Profiler& profiler);
	void drawIndirectCommands(FrameContext& frameCtx, GPUResources& resources, Profiler& profiler);
}