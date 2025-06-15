#pragma once

#include <core/types/Texture.h>
#include "common/ResourceTypes.h"

struct LoadedGLTF : public IRenderable {
	// === GPU Runtime Assets ===
	std::vector<std::shared_ptr<MeshHandle>> meshes;         // Indexed by GLTF mesh index
	std::vector<std::shared_ptr<Node>> nodes;                // Indexed by GLTF node index
	std::vector<std::shared_ptr<MaterialHandle>> materials;  // Indexed by GLTF material index

	std::vector<AllocatedImage> images;      // Indexed by GLTF image index
	std::vector<VkSampler> samplers;         // Indexed by GLTF sampler index
	std::vector<fastgltf::Texture> textures; // If you need them later in material processing

	// === Scene Hierarchy ===
	std::vector<std::shared_ptr<Node>> topNodes; // Root-level nodes (for tree traversal)

	// === Scene Info ===
	std::string sceneName;
	bool enableCull = true;
	std::filesystem::path basePath;       // File path (used for relative asset loading)

	// === Interface ===
	~LoadedGLTF() { clearAll(); }

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx);

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
	{ SceneID::DamagedHelmet, "damagedhelemt" },
};

namespace AssetManager {
	bool loadGltf(ThreadContext& threadCtx);
	void decodeImages(ThreadContext& threadCtx, const VmaAllocator allocator, DeletionQueue& bufferQueue);
	void buildSamplers(ThreadContext& threadCtx);
	void processMaterials(ThreadContext& threadCtx, const VmaAllocator allocator);
	void processMeshes(ThreadContext& threadCtx);
}