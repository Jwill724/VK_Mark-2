#pragma once

//#include <unordered_map>
#include <vector>

class ModelAsset;
class Scene;
class FrameContext;
class Allocator;
class Profiler;
class BindlessImageTable;
struct GLFWwindow;
struct Mesh;
struct MeshLODs;

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
	EmissiveTest,
	WrathDragon,
	Mech,
	YellowMech,
	Mini,

	Count
};

namespace World
{
	Scene& GetScene(); // Scary global reference

	//inline std::unordered_map<ModelID, std::shared_ptr<ModelAsset>> _loadedScenes;

	void Init(const BindlessImageTable& renderer, bool isAssetsLoaded);
	void Cleanup();

	void UpdateWorldState(
		FrameContext& frameCtx,
		Allocator& allocator,
		Profiler& profiler,
		GLFWwindow* window);

	void UpdateDrawData(
		FrameContext& frameCtx,
		const std::vector<Mesh>& meshes,
		const std::vector<MeshLODs>& meshLODs,
		const Profiler& profiler);
}
