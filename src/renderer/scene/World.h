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
struct Extents2D;

enum class ModelID // TODO: This goes somewhere else, not here
{
	Sponza,
	SponzaIntelMain,
	SponzaIntelCurtains,
	SponzaIntelIvy,
	SponzaIntelTree,
	SanMiguel,
	Bistro,
	BistroExt,
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

	//DarkRoom,
	//CornellBox,
	//BreakfastRoom,
	//FireplaceRoom,
	//Conference,
	//Sibenik,
	//MandarinOrange,
	//CandleHolder,
	//TransmissionTest,
	//CompareClearCoat,
	//MosquitoInAmber,

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
		uint32_t frameNumber,
		const Extents2D& drawExtent,
		FrameContext& frameCtx,
		Allocator& allocator,
		Profiler& profiler,
		GLFWwindow* window,
		bool isTemporalAllowed);
}
