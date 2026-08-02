#pragma once

#include <unordered_map>
#include <memory>

class Scene;
class FrameContext;
class Allocator;
class Profiler;
class BindlessImageTable;
struct ModelAsset;
struct GLFWwindow;
struct InstanceState;

enum class ModelID // TODO: This goes somewhere else, not here
{
	Sponza,
	Bistro,
	MRSpheres,
	Duck,
	DamagedHelmet,
	DragonAttenuation,
	City,
	Structure,
	WrathDragon,
	Mech,
	YellowMech,
	Mini,

	Count
};

namespace World
{
	Scene& GetScene(); // Scary global reference
	InstanceState& GetInstanceState();

	inline std::unordered_map<ModelID, std::shared_ptr<ModelAsset>> _loadedScenes;

	void OnSceneLoaded(std::shared_ptr<ModelAsset> asset);

	void Init(const BindlessImageTable& renderer);
	void Cleanup();

	void UpdateWorldState(
		FrameContext& frameCtx,
		Allocator& allocator,
		Profiler& profiler,
		GLFWwindow* window);
}
