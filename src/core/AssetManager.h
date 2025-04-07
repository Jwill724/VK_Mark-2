#pragma once

#include "common/Vk_Types.h"
#include <unordered_map>
#include <filesystem>

namespace AssetManager {

	std::vector<AllocatedImage>& getTexImages();
	AllocatedImage& getWhiteImage();
	AllocatedImage& getCheckboardTex();

	VkSampler getDefaultSamplerLinear();
	VkSampler getDefaultSamplerNearest();

	void loadAssets();
	std::vector<std::shared_ptr<MeshAsset>>& getTestMeshes();

	std::optional<std::vector<std::shared_ptr<MeshAsset>>> loadGltfMeshes(const std::filesystem::path& filePath);

	ImmCmdSubmitDef& getGraphicsCmdSubmit();

	DeletionQueue& getAssetDeletionQueue();
	VmaAllocator& getAssetAllocation();

	// MESHES
	GPUMeshBuffers uploadMesh(std::span<uint32_t> indices, std::span<Vertex> vertices);
}