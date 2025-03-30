#pragma once

#include "common/Vk_Types.h"
#include <unordered_map>
#include <filesystem>

struct GeoSurface {
	uint32_t startIndex;
	uint32_t count;
};

struct MeshAsset {
	std::string name;

	std::vector<GeoSurface> surfaces;
	GPUMeshBuffers meshBuffers;
};

namespace AssetManager {

	void loadAssets();
	std::vector<std::shared_ptr<MeshAsset>>& getTestMeshes();

	std::optional<std::vector<std::shared_ptr<MeshAsset>>> loadGltfMeshes(const std::filesystem::path& filePath);

	DeletionQueue& getAssetDeletionQueue();
	VmaAllocator& getAssetAllocation();
	// MESHES
	GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);
	void transformMesh(GPUDrawPushConstants& pushConstants, float aspect);
}