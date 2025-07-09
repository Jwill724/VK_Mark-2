#pragma once

#include <core/types/Texture.h>
#include "common/ResourceTypes.h"
#include "renderer/SceneGraph.h"

struct ModelAsset : public IRenderable {
	struct GPUData {
		std::unordered_map<uint32_t, std::vector<std::shared_ptr<BakedInstance>>> nodeIndexToBakedInstances;
		std::vector<AllocatedImage> images;
		std::vector<VkSampler> samplers;
		std::vector<GPUMaterial> materials;
	} runtime;

	struct SceneGraph {
		std::vector<std::shared_ptr<Node>> nodes;
		// nodes that don't have a parent, for iterating through the file in tree order
		std::vector<std::shared_ptr<Node>> topNodes;

		std::vector<uint32_t> nodeIDToTransformID;
	} scene;

	std::string sceneName;
	std::filesystem::path basePath;

	~ModelAsset() { clearAll(); }

	virtual void FindVisibleInstances(
		std::vector<GPUInstance>& outVisibleOpaqueInstances,
		std::vector<GPUInstance>& outVisibleTransparentInstances,
		std::vector<glm::mat4>& outFrameTransformsList,
		const std::vector<glm::mat4>& bakedTransformsList,
		const std::unordered_set<uint32_t>& visibleMeshIDSet) override;

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
	void processMeshes(
		ThreadContext& threadCtx,
		std::vector<GPUDrawRange>& drawRanges,
		MeshRegistry& meshes,
		std::vector<Vertex>& vertices,
		std::vector<uint32_t>& indices);
}