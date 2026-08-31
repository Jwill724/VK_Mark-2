#pragma once

#include "EngineTypes.h"
#include "SceneSource.h"

inline static const std::string BaseAssetPath = "res/assets/";

struct AssetEntry
{
	std::string   relativePath;
	ImportOptions options{};
};

const std::unordered_map<ModelID, AssetEntry>& GetAssetRegistry();
