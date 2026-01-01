#pragma once

#include "core/AssetManager.h"
#include "core/ResourceManager.h"
#include "renderer/frame/FrameContext.h"
#include "engine/platform/input/Camera.h"

struct SceneProfileEntry {
	std::string name;
	DrawType drawType;
	uint32_t instanceCount = 1;
};

// Holds and controls scene data
namespace RenderScene {
	GPUSceneData& getCurrentSceneData();
	GPUShadowCSM& getShadowCSM();

	inline std::unordered_map<SceneID, std::shared_ptr<ModelAsset>> _loadedScenes;

	inline std::unordered_map<SceneID, SceneProfileEntry> _sceneProfiles {
		{ SceneID::Sponza, { "Sponza", DrawType::DrawStatic, 1 } },
		{ SceneID::Bistro, { "Bistro", DrawType::DrawStatic, 1 } },
		{ SceneID::MRSpheres, { "MRSpheres", DrawType::DrawStatic, 1 } },
		{ SceneID::Duck, { "Duck", DrawType::DrawMultiStatic, 1000 } },
		{ SceneID::DamagedHelmet, { "DamagedHelmet", DrawType::DrawMultiStatic, 100 } },
		{ SceneID::DragonAttenuation, { "Dragon", DrawType::DrawStatic, 1 } },
		{ SceneID::City, { "City", DrawType::DrawStatic, 1 } },
		{ SceneID::Structure, { "Structure", DrawType::DrawStatic, 1 } },
		{ SceneID::EmissiveTest, { "EmissiveTest", DrawType::DrawStatic, 1 } },
		{ SceneID::WrathDragon, { "WrathDragon", DrawType::DrawStatic, 1 } },
	};

	extern std::vector<GlobalInstance> _globalInstances;
	extern std::vector<glm::mat4> _globalTransforms;

	extern ShadowControl _shadowControl;

	const Camera& getCamera();

	void setScene(bool assetsLoaded);

	void cleanScene(GPUAddressTable& globalTable);

	void updateScene(FrameContext& frameCtx, GPUResources& gpuResources, const DebugToggles& debug);
}