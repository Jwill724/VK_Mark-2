#pragma once

#include "AssetUploadTypes.h"
#include "SceneSource.h"
#include "EngineTypes.h"
#include <atomic>

class JobSystem;
struct SceneJobContext;
struct StageQueues;

using SceneBatchReadyCallback = std::function<void(SceneUploadBatch&&)>;

class AssetManager
{
public:
	void LoadScenes(SceneBatchReadyCallback onBatchReady, JobSystem& jobSystem);
	void Shutdown(JobSystem& jobSystem);

private:
	bool StageLoadFile(ThreadContext& ctx);
	bool StageDecodeImages(ThreadContext& ctx);
	bool StageBuildSamplers(ThreadContext& ctx);
	bool StageProcessMaterials(ThreadContext& ctx);
	bool StageProcessMeshes(ThreadContext& ctx);
	bool StageBuildSceneGraph(ThreadContext& ctx);

	std::shared_ptr<StageQueues> m_queues;
};

struct SceneJobContext
{
	std::shared_ptr<SceneUploadBatch> batch;
	SourceScene                       source;
	std::filesystem::path             filePath;
	ImportOptions                     importOptions{};
	bool                              fromCache = false;
	std::atomic<bool>                 graphQueued{ false };

	std::array<std::atomic<bool>, static_cast<size_t>(AssetJobType::Count)> jobComplete{};

	void MarkComplete(AssetJobType t) { jobComplete[static_cast<size_t>(t)] = true; }
	bool IsComplete(AssetJobType t) const { return jobComplete[static_cast<size_t>(t)]; }

	bool CanRun(AssetJobType stage) const
	{
		switch (stage)
		{
		case AssetJobType::LoadFile:
			return true;
		case AssetJobType::DecodeImages:
		case AssetJobType::BuildSamplers:
		case AssetJobType::ProcessMeshes:
			return IsComplete(AssetJobType::LoadFile);
		case AssetJobType::ProcessMaterials:
			return IsComplete(AssetJobType::BuildSamplers);
		case AssetJobType::BuildSceneGraph:
			return IsComplete(AssetJobType::DecodeImages)
				&& IsComplete(AssetJobType::ProcessMaterials)
				&& IsComplete(AssetJobType::ProcessMeshes);
		default:
			return false;
		}
	}
};

struct StageQueues : BaseWorkQueue
{
	TypedWorkQueue<std::shared_ptr<SceneJobContext>> loadFile;
	TypedWorkQueue<std::shared_ptr<SceneJobContext>> decodeImages;
	TypedWorkQueue<std::shared_ptr<SceneJobContext>> buildSamplers;
	TypedWorkQueue<std::shared_ptr<SceneJobContext>> processMaterials;
	TypedWorkQueue<std::shared_ptr<SceneJobContext>> processMeshes;
	TypedWorkQueue<std::shared_ptr<SceneJobContext>> buildSceneGraph;

	SceneBatchReadyCallback onBatchReady;
	JobSystem* jobSystem = nullptr;
	std::atomic<uint32_t>   pendingSceneCount{ 0 };

	bool IsFullyDrained() const noexcept { return pendingSceneCount.load() == 0; }

	void Finish(std::shared_ptr<SceneJobContext> ctx)
	{
		pendingSceneCount--;
		if (onBatchReady) onBatchReady(std::move(*ctx->batch));
	}

	void Advance(std::shared_ptr<SceneJobContext> ctx, AssetJobType completedStage)
	{
		ctx->MarkComplete(completedStage);

		switch (completedStage)
		{
		case AssetJobType::LoadFile:
			if (ctx->fromCache) { Finish(ctx); break; }
			decodeImages.Push(ctx);
			buildSamplers.Push(ctx);
			processMeshes.Push(ctx);
			break;

		case AssetJobType::DecodeImages:
			if (ctx->CanRun(AssetJobType::BuildSceneGraph))
				buildSceneGraph.Push(ctx);
			break;

		case AssetJobType::BuildSamplers:
			processMaterials.Push(ctx);
			break;

		case AssetJobType::ProcessMaterials:
		case AssetJobType::ProcessMeshes:
			if (ctx->CanRun(AssetJobType::BuildSceneGraph))
				buildSceneGraph.Push(ctx);
			break;

		case AssetJobType::BuildSceneGraph:
			Finish(ctx);
			break;

		default:
			break;
		}
	}
};
