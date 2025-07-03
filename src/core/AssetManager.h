#pragma once

#include <core/types/Texture.h>
#include "common/ResourceTypes.h"

struct ModelAsset : public IRenderable {
	struct GPUData {
		std::vector<std::shared_ptr<RenderInstance>> instances;
		std::vector<AllocatedImage> images;
		std::vector<VkSampler> samplers;
		std::vector<GPUMaterial> materials;
	} gpu;

	struct SceneGraph {
		std::vector<std::shared_ptr<Node>> nodes;

		// nodes that dont have a parent, for iterating through the file in tree order
		std::vector<std::shared_ptr<Node>> topNodes;
	} scene;

	std::string sceneName;
	std::filesystem::path basePath;

	~ModelAsset() { clearAll(); }


	// Definitions for member functions in SceneGraph.cpp because why not
	virtual void FindVisibleObjects(
		std::vector<GPUInstance>& outOpaqueVisibles,
		std::vector<GPUInstance>& outTransparentVisibles,
		const std::unordered_map<uint32_t, std::vector<glm::mat4>>& meshIDToTransformMap,
		const std::unordered_set<uint32_t>& visibleMeshIDSet) override;

	void bakeTransformsPerMesh(std::unordered_map<uint32_t, std::vector<glm::mat4>>& outMeshTransforms)
	{
		for (const auto& node : scene.topNodes) {
			if (node) {
				if (auto meshNode = std::dynamic_pointer_cast<MeshNode>(node)) {
					bakeMeshNodeTransforms(meshNode, glm::mat4(1.0f), outMeshTransforms);
				}
			}
		}

	}

private:
	void clearAll();

	void bakeMeshNodeTransforms(
		const std::shared_ptr<MeshNode>& meshNode,
		const glm::mat4& parentMatrix,
		std::unordered_map<uint32_t, std::vector<glm::mat4>>& outMeshTransforms);
};


struct GLTFJobContext {
	std::shared_ptr<ModelAsset> scene;
	fastgltf::Asset gltfAsset;
	UploadMeshContext uploadMeshCtx;

	// Set to true when scene is passed into loadedscenes
	std::atomic<bool> hasRegisteredScene = false;

	std::atomic<bool> jobComplete[sizeof(GLTFJobType)];

	void markJobComplete(GLTFJobType type) {
		jobComplete[static_cast<size_t>(type)] = true;
	}

	bool isJobComplete(GLTFJobType type) const {
		return jobComplete[static_cast<size_t>(type)];
	}

	bool isComplete() const {
		for (bool status : jobComplete)
			if (!status) return false;
		return true;
	}
};

using GLTFAssetQueue = TypedWorkQueue<std::shared_ptr<GLTFJobContext>>;

enum class SceneID {
	Sponza,
	MRSpheres,
	Cube,
	DamagedHelmet,
	DragonAttenuation
};

static const std::unordered_map<SceneID, std::string> SceneNames = {
	{ SceneID::Sponza, "sponza" },
	{ SceneID::MRSpheres, "mrspheres" },
	{ SceneID::Cube, "cube" },
	{ SceneID::DamagedHelmet, "damagedhelmet" },
	{ SceneID::DragonAttenuation, "dragon" },
};

namespace AssetManager {
	bool loadGltf(ThreadContext& threadCtx);
	void decodeImages(ThreadContext& threadCtx, const VmaAllocator allocator, DeletionQueue& bufferQueue);
	void buildSamplers(ThreadContext& threadCtx);
	void processMaterials(ThreadContext& threadCtx, const VmaAllocator allocator);
	void processMeshes(ThreadContext& threadCtx, std::vector<GPUDrawRange>& drawRanges, MeshRegistry& meshes);
}