#pragma once

#include "AssetUploadTypes.h"
#include "EngineTypes.h"
#include <fastgltf/types.hpp>
#include <atomic>

class JobSystem;
struct GLTFJobContext;
struct StageQueues;

inline static const std::string BaseAssetPath = "res/assets/";

using SceneBatchReadyCallback = std::function<void(SceneUploadBatch&&)>;

class AssetManager
{
public:
	void LoadScenes(
		SceneBatchReadyCallback      onBatchReady,
		JobSystem&                   jobSystem);

	void Shutdown(JobSystem& jobSystem);

private:
	bool StageLoadFile        (ThreadContext& ctx);
	bool StageDecodeImages    (ThreadContext& ctx);
	bool StageBuildSamplers   (ThreadContext& ctx);
	bool StageProcessMaterials(ThreadContext& ctx);
	bool StageProcessMeshes   (ThreadContext& ctx);
	bool StageBuildSceneGraph (ThreadContext& ctx);

	std::shared_ptr<StageQueues> m_queues;
	std::unordered_map<ModelID, SceneUploadBatch> m_pendingBatches;
	std::mutex m_batchMutex;
};

struct GLTFJobContext
{
	std::shared_ptr<SceneUploadBatch> batch;
	fastgltf::Asset                   gltfAsset;
	std::filesystem::path             basePath;

	std::atomic<bool> bHasRegisteredScene = false;
	std::array<std::atomic<bool>, static_cast<size_t>(GLTFJobType::Count)> jobComplete{};

	void MarkComplete(GLTFJobType t) { jobComplete[static_cast<size_t>(t)] = true; }
	bool IsComplete(GLTFJobType t) const { return jobComplete[static_cast<size_t>(t)]; }
	bool IsFullyComplete() const
	{
		for (const auto& b : jobComplete) if (!b) return false;
		return true;
	}

	bool CanRun(GLTFJobType stage) const
	{
		switch (stage)
		{
		case GLTFJobType::LoadFile:
			return true;
		case GLTFJobType::DecodeImages:
		case GLTFJobType::BuildSamplers:
		case GLTFJobType::ProcessMeshes:
			return IsComplete(GLTFJobType::LoadFile);
		case GLTFJobType::ProcessMaterials:
			return IsComplete(GLTFJobType::DecodeImages)
				&& IsComplete(GLTFJobType::BuildSamplers);
		case GLTFJobType::BuildSceneGraph:
			return IsComplete(GLTFJobType::ProcessMaterials)
				&& IsComplete(GLTFJobType::ProcessMeshes);
		default:
			return false;
		}
	}
};

struct StageQueues : BaseWorkQueue
{
	TypedWorkQueue<std::shared_ptr<GLTFJobContext>> loadFile;
	TypedWorkQueue<std::shared_ptr<GLTFJobContext>> decodeImages;
	TypedWorkQueue<std::shared_ptr<GLTFJobContext>> buildSamplers;
	TypedWorkQueue<std::shared_ptr<GLTFJobContext>> processMaterials;
	TypedWorkQueue<std::shared_ptr<GLTFJobContext>> processMeshes;
	TypedWorkQueue<std::shared_ptr<GLTFJobContext>> buildSceneGraph;

	SceneBatchReadyCallback onBatchReady;
	std::atomic<uint32_t>   pendingSceneCount{ 0 };

	bool IsFullyDrained() const noexcept { return pendingSceneCount.load() == 0; }

	void Advance(std::shared_ptr<GLTFJobContext> ctx, GLTFJobType completedStage)
	{
		ctx->MarkComplete(completedStage);

		switch (completedStage)
		{
		case GLTFJobType::LoadFile:
			decodeImages.Push(ctx);
			buildSamplers.Push(ctx);
			processMeshes.Push(ctx);
			break;

		case GLTFJobType::DecodeImages:
		case GLTFJobType::BuildSamplers:
			if (ctx->CanRun(GLTFJobType::ProcessMaterials))
				processMaterials.Push(ctx);
			break;

		case GLTFJobType::ProcessMaterials:
		case GLTFJobType::ProcessMeshes:
			if (ctx->CanRun(GLTFJobType::BuildSceneGraph))
				buildSceneGraph.Push(ctx);
			break;

		case GLTFJobType::BuildSceneGraph:
			pendingSceneCount--;
			if (onBatchReady)
				onBatchReady(std::move(*ctx->batch));
			break;

		default:
			break;
		}
	}
};
