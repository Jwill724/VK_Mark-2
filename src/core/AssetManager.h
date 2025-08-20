#pragma once

#include <core/loader/TextureLoader.h>
#include "renderer/scene/SceneGraph.h"

struct ModelAsset {
	struct GPUData {
		std::vector<std::shared_ptr<GPUInstance>> bakedInstances;
		std::vector<AllocatedImage> images;
		std::vector<VkSampler> samplers;
		std::vector<GPUMaterial> materials;
	} runtime;

	struct SceneGraphNodes {
		std::vector<std::shared_ptr<SceneGraph::Node>> nodes;
		// nodes that don't have a parent, for iterating through the file in tree order
		std::vector<std::shared_ptr<SceneGraph::Node>> topNodes;
	} sceneNodes;

	SceneID sceneID = SceneID::Count;
	std::string sceneName;
	std::filesystem::path basePath;

	~ModelAsset() { clearAll(); }

private:
	void clearAll();
};


struct GLTFJobContext {
	std::shared_ptr<ModelAsset> scene;
	fastgltf::Asset gltfAsset;

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

namespace AssetManager {
	bool loadGltf(ThreadContext& threadCtx);
	void decodeImages(
		ThreadContext& threadCtx,
		VmaAllocator allocator,
		DeletionQueue& bufferQueue,
		const VkDevice device);
	void buildSamplers(ThreadContext& threadCtx);
	void processMaterials(ThreadContext& threadCtx, const VmaAllocator allocator, const VkDevice device);
	void processMeshes(
		ThreadContext& threadCtx,
		MeshRegistry& meshes,
		std::vector<Vertex>& vertices,
		std::vector<uint32_t>& indices);
}