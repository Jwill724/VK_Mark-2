#include "pch.h"
#include "AssetRegistry.h"

const std::unordered_map<ModelID, AssetEntry>& GetAssetRegistry()
{
	static const std::unordered_map<ModelID, AssetEntry> registry
	{
		{ ModelID::Sponza,            { "sponza.glb", {.importScale = 1.0f } }},
		//{ ModelID::SponzaIntelMain,     { "sponza_intel/main/NewSponza_Main_glTF_003.gltf", {.importScale = 1.0f } }},
		//{ ModelID::SponzaIntelCurtains, { "sponza_intel/curtains/NewSponza_Curtains_glTF.gltf", {.importScale = 1.0f } }},
		//{ ModelID::SponzaIntelIvy,      { "sponza_intel/ivy/NewSponza_IvyGrowth_glTF.gltf", {.importScale = 1.0f } }},
		//{ ModelID::SponzaIntelTree,     { "sponza_intel/tree/NewSponza_CypressTree_glTF.gltf", {.importScale = 1.0f } }},
		//{ ModelID::BistroExt,            { "bistro/gltf/bistro.gltf", {.importScale = 0.01f}}},
		//{ ModelID::SanMiguel,         { "San_Miguel/gltf/san-miguel.gltf", {.importScale = 1.0f}}},
		//{ ModelID::MRSpheres,         { "MetalRoughSpheres.glb", {.importScale = 1.0f}}},
		{ ModelID::DamagedHelmet,     { "DamagedHelmet.glb", {.importScale = 1.0f}}},
		//{ ModelID::Bistro,            { "bistro.glb", {.importScale = 1.0f}}},
		//{ ModelID::Duck,              { "Duck.glb", {.importScale = 1.0f}}}
		//{ ModelID::DragonAttenuation, { "DragonAttenuation.glb", {.importScale = 1.0f }}},
		//{ ModelID::City,              { "city/town4new.glb", {.importScale = 1.0f}}},
		//{ ModelID::Structure,         { "structure.glb", {.importScale = 1.0f}}},
		//{ ModelID::WrathDragon,       { "wrath_of_the_dragon.glb", {.importScale = 1.0f}}},
		//{ ModelID::Mech,              { "mech.glb", {.importScale = 1.0f}}},
		//{ ModelID::YellowMech,        { "yellow_mech.glb", {.importScale = 1.0f}}},
		//{ ModelID::Mini,              { "mini.glb", {.importScale = 1.0f}}},
	};
	return registry;
}
