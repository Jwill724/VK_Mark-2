#pragma once

#include <core/loader/TextureLoader.h>

// TODO: Redesign this scene id system

// ====== Scene Graph Node Base ======
struct Node {
	std::weak_ptr<Node> parent;
	std::vector<std::shared_ptr<Node>> children;

	glm::mat4 localTransform{ 1.0f };
	glm::mat4 worldTransform{ 1.0f };

	inline void refreshTransform(const glm::mat4& parentMatrix) {
		worldTransform = parentMatrix * localTransform;
		for (auto& c : children) {
			if (c) c->refreshTransform(worldTransform);
		}
	}
};

enum class SceneID : uint8_t {
	Sponza,
	Bistro,
	MRSpheres,
	Duck,
	DamagedHelmet,
	DragonAttenuation,
	City,
	Structure,
	EmissiveTest,
	WrathDragon,
	Mech,
	YellowMech,
	Mini,

	Count
};

struct ModelAsset {
	// At creation bakes meshes, materials, pass types for each instance.
	// Stores all vertex and index data to access those buffers.
	// The ORIGINAL DATA of a model is kept here untouched as a singular entity,
	// no transformID or draw type is known in here as thats handled at runtime.
	// Nodes = the transform count in an asset.
	struct GPUData {
		std::vector<GPUInstance> bakedInstances;
		size_t vertexOffset = 0;
		size_t indexOffset = 0;
		size_t vertexCount = 0;
		size_t indexCount = 0;
		uint32_t localMaterialCount = 0;
		size_t materialBaseOffset = 0;
		std::vector<RuntimeImage> images;
		std::vector<VkSampler> samplers;
		std::vector<GPUMaterial> materials;

		std::vector<uint32_t> bakedNodeIDs;    // nodes to search each inner transform tree
		std::vector<uint32_t> uniqueNodeIDs;   // compact list of node indices that own a transform
		std::vector<uint32_t> localToNodeSlot; // primitive i -> node slot in uniqueNodeIDs
	} runtime;

	struct SceneGraphNodes {
		std::vector<std::shared_ptr<Node>> nodes;
		// nodes that don't have a parent, for iterating through the file in tree order
		std::vector<std::shared_ptr<Node>> topNodes;
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

	std::array<std::atomic<bool>, static_cast<size_t>(GLTFJobType::Count)> jobComplete;

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
	static const std::unordered_map<SceneID, std::string> SceneNames{
		{ SceneID::Sponza, "Sponza" },
		{ SceneID::Bistro, "Bistro" },
		{ SceneID::MRSpheres, "MRSpheres" },
		{ SceneID::Duck, "Duck" },
		{ SceneID::DamagedHelmet, "DamagedHelmet" },
		{ SceneID::DragonAttenuation, "Dragon" },
		{ SceneID::City, "City" },
		{ SceneID::Structure, "Structure" },
		{ SceneID::EmissiveTest, "EmissiveTest" },
		{ SceneID::WrathDragon, "WrathDragon" },
		{ SceneID::Mech, "Mech" },
		{ SceneID::YellowMech, "YellowMech" },
		{ SceneID::Mini, "Mini" },
	};

	static const std::unordered_map<std::string, SceneID> SceneIDs{
		{ "Sponza", SceneID::Sponza },
		{ "Bistro", SceneID::Bistro },
		{ "MRSpheres", SceneID::MRSpheres },
		{ "Duck", SceneID::Duck },
		{ "DamagedHelmet", SceneID::DamagedHelmet },
		{ "Dragon", SceneID::DragonAttenuation },
		{ "City", SceneID::City },
		{ "Structure", SceneID::Structure },
		{ "EmissiveTest", SceneID::EmissiveTest },
		{ "WrathDragon", SceneID::WrathDragon },
		{ "Mech", SceneID::Mech },
		{ "YellowMech", SceneID::YellowMech },
		{ "Mini", SceneID::Mini },
	};

	bool loadGltf(ThreadContext& threadCtx);
	std::optional<std::shared_ptr<GLTFJobContext>> loadGltfFiles(std::string_view filePath);
	void decodeImages(
		ThreadContext& threadCtx,
		VmaAllocator allocator,
		DeletionQueue& bufferQueue,
		const VkDevice device);
	void buildSamplers(ThreadContext& threadCtx);
	void processMaterials(
		ThreadContext& threadCtx,
		const VmaAllocator allocator,
		const VkDevice device);
	void processMeshes(
		ThreadContext& threadCtx,
		MeshRegistry& meshes,
		std::vector<Vertex>& vertices,
		std::vector<uint32_t>& indices,
		ModelDataCounts& modelDataCounts);

	void buildSceneGraph(
		ThreadContext& threadCtx,
		std::vector<GlobalInstance>& globalInstances,
		std::vector<glm::mat4>& globalTransforms,
		ModelDataCounts& modelDataCounts);
}
