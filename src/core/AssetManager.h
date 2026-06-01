#pragma once

//#include "AssetUploadTypes.h"
//#include "EngineTypes.h"
//#include <fastgltf/types.hpp>
//#include <functional>
//#include <atomic>
//
//inline static const std::string BaseAssetPath = "res/assets/";
//
//class ModelAsset;
//
//struct GLTFJobContext;
//using GLTFAssetQueue = TypedWorkQueue<std::shared_ptr<GLTFJobContext>>;
//
//// Called by Renderer when a SceneUploadBatch is ready to upload
//using SceneBatchReadyCallback = std::function<void(SceneUploadBatch&&)>;
//
//class AssetManager
//{
//public:
//	// Kicks off the full pipeline for a set of glTF paths.
//	// When all stages for a scene are complete, fires onBatchReady
//	// on the calling thread (or a Renderer thread — caller decides).
//	void LoadScenes(
//		std::span<const std::string> paths,
//		SceneBatchReadyCallback      onBatchReady);
//
//	// Individual pipeline stages — each operates on the queue
//	// and advances GLTFJobContext through its job flags.
//	// All produce data into SceneUploadBatch, no GPU calls.
//
//	void StageLoadGltf    (ThreadContext& ctx, GLTFAssetQueue& queue);
//	void StageDecodeImages(ThreadContext& ctx, GLTFAssetQueue& queue);
//	void StageBuildSamplers(ThreadContext& ctx, GLTFAssetQueue& queue);
//	void StageProcessMaterials(ThreadContext& ctx, GLTFAssetQueue& queue);
//	void StageProcessMeshes(ThreadContext& ctx, GLTFAssetQueue& queue);
//	void StageBuildSceneGraph(ThreadContext& ctx, GLTFAssetQueue& queue);
//
//	// Lifetime management — AssetManager tells Renderer when to free
//	void RequestUnload(ModelID sceneID, SceneBatchReadyCallback onUnloadReady);
//
//private:
//	std::unordered_map<ModelID, SceneUploadBatch> m_pendingBatches;
//	std::mutex m_batchMutex;
//};
//
//class ModelAsset
//{
//	friend class AssetManager;
//public:
//	static struct GPUData
//	{
//		std::vector<VirtualInstance> bakedInstances;
//		size_t vertexOffset = 0;
//		size_t indexOffset = 0;
//		size_t vertexCount = 0;
//		size_t indexCount = 0;
//		uint32_t localMaterialCount = 0;
//		size_t materialBaseOffset = 0;
//		std::vector<uint32_t> textureIds;
//		std::vector<uint32_t> samplersIds;
//		std::vector<uint32_t> materialIds;
//		std::vector<bool> normalMapFlags;
//
//		std::vector<uint32_t> bakedNodeIDs;    // nodes to search each inner transform tree
//		std::vector<uint32_t> uniqueNodeIDs;   // compact list of node indices that own a transform
//		std::vector<uint32_t> localToNodeSlot; // primitive i -> node slot in uniqueNodeIDs
//	};
//
//	static struct SceneGraphNodes
//	{
//		std::vector<std::shared_ptr<Node>> nodes;
//		// nodes that don't have a parent, for iterating through the file in tree order
//		std::vector<std::shared_ptr<Node>> topNodes;
//	};
//
//	ModelID sceneID = ModelID::Count;
//	std::string sceneName;
//	std::filesystem::path basePath;
//
//	GPUData runtime;
//	SceneGraphNodes sceneNodes;
//
//private:
//	void ClearAll();
//};
//
//struct GLTFJobContext
//{
//	std::shared_ptr<SceneUploadBatch> batch;   // accumulates all stage output
//	fastgltf::Asset                   gltfAsset;
//	std::filesystem::path             basePath;
//
//	// Set to true when scene is passed into loadedscenes
//	std::atomic<bool> bHasRegisteredScene = false;
//
//	std::array<std::atomic<bool>, static_cast<size_t>(GLTFJobType::Count)> jobComplete;
//
//	constexpr void MarkJobComplete(GLTFJobType type)
//	{
//		jobComplete[static_cast<size_t>(type)] = true;
//	}
//
//	constexpr bool IsJobComplete(GLTFJobType type) const noexcept
//	{
//		return jobComplete[static_cast<size_t>(type)];
//	}
//
//	constexpr bool IsComplete() const noexcept
//	{
//		for (bool status : jobComplete)
//			if (!status) return false;
//		return true;
//	}
//};
