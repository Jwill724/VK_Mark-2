#pragma once

#include <core/types/Texture.h>
#include "common/ResourceTypes.h"

struct LoadedGLTF : public IRenderable {
	struct GPUData {
		std::vector<std::shared_ptr<SceneObject>> sceneObjs;
		std::vector<AllocatedImage> images;
		std::vector<VkSampler> samplers;
		std::vector<PBRMaterial> materials;
	} gpu;

	// CPU-side scene data
	struct SceneGraph {
		std::vector<std::shared_ptr<Node>> nodes;
		std::vector<std::shared_ptr<Node>> topNodes;
	} scene;

	std::string sceneName;
	std::filesystem::path basePath;

	// === Interface ===
	~LoadedGLTF() { clearAll(); }

	virtual void FlattenSceneToTransformList(const glm::mat4& topMatrix, std::unordered_map<uint32_t, glm::mat4>& meshIDToTransformMap);
	virtual void FindVisibleObjects(
		std::vector<GPUInstance>& outOpaqueVisibles,
		std::vector<GPUInstance>& outTransparentVisibles,
		const std::unordered_map<uint32_t, glm::mat4>& meshIDToTransformMap,
		const std::unordered_set<uint32_t>& visibleMeshIDSet);

private:
	void clearAll();
};

struct GLTFJobContext {
	std::shared_ptr<LoadedGLTF> scene;
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
};

static const std::unordered_map<SceneID, std::string> SceneNames = {
	{ SceneID::Sponza, "sponza" },
	{ SceneID::MRSpheres, "mrspheres" },
	{ SceneID::Cube, "cube" },
	{ SceneID::DamagedHelmet, "damagedhelmet" },
};

namespace AssetManager {
	bool loadGltf(ThreadContext& threadCtx);
	void decodeImages(ThreadContext& threadCtx, const VmaAllocator allocator, DeletionQueue& bufferQueue);
	void buildSamplers(ThreadContext& threadCtx);
	void processMaterials(ThreadContext& threadCtx, const VmaAllocator allocator);
	void processMeshes(ThreadContext& threadCtx, std::vector<GPUDrawRange>& drawRanges, MeshRegistry& meshes);
}