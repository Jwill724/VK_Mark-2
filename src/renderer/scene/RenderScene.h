#pragma once

#include "core/AssetManager.h"
#include "core/ResourceManager.h"
#include "renderer/frame/FrameContext.h"
#include "engine/platform/input/Camera.h"
#include "LightingSystem.h"

struct SceneProfileEntry {
	std::string name;
	InstancingMethod drawType;
	uint32_t instanceCount = 1;
};

// Holds and controls scene data
namespace RenderScene {
	SceneInfo& getCurrentSceneData();
	DirectionalCSMInfo& getShadowCSM();

	inline std::unordered_map<SceneID, std::shared_ptr<ModelAsset>> _loadedScenes;

	inline std::unordered_map<SceneID, SceneProfileEntry> _sceneProfiles {
		{ SceneID::Sponza, { "Sponza", InstancingMethod::DrawStatic, 1 } },
		{ SceneID::Bistro, { "Bistro", InstancingMethod::DrawStatic, 1 } },
		{ SceneID::MRSpheres, { "MRSpheres", InstancingMethod::DrawStatic, 1 } },
		{ SceneID::Duck, { "Duck", InstancingMethod::DrawStatic, 1 } },
		{ SceneID::DamagedHelmet, { "DamagedHelmet", InstancingMethod::DrawStatic, 1 } },
		{ SceneID::DragonAttenuation, { "Dragon", InstancingMethod::DrawStatic, 1 } },
		{ SceneID::City, { "City", InstancingMethod::DrawStatic, 1 } },
		{ SceneID::Structure, { "Structure", InstancingMethod::DrawStatic, 1 } },
		{ SceneID::EmissiveTest, { "EmissiveTest", InstancingMethod::DrawStatic, 1 } },
		{ SceneID::WrathDragon, { "WrathDragon", InstancingMethod::DrawStatic, 1 } },
		{ SceneID::Mech, { "Mech", InstancingMethod::DrawStatic, 1 } },
		{ SceneID::YellowMech, { "YellowMech", InstancingMethod::DrawStatic, 1 } },
		{ SceneID::Mini, { "Mini", InstancingMethod::DrawStatic, 1 } },
	};

	extern std::vector<VirtualInstance> _globalInstances;
	extern std::vector<glm::mat4> _globalTransforms;

	extern ShadowControl _shadowControl;

	extern DispatchList _dispatchListSSS;

	Camera& getCamera();
	const Frustum& getMainFrustum();
	const glm::mat4& getCurProjUnjittered();

	void setScene(bool assetsLoaded);

	void cleanScene();

	void updateScene(
		FrameContext& frameCtx,
		GPUResources& gpuResources,
		const RenderToggles& debug);
}
