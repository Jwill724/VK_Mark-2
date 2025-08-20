#pragma once

#include "core/ResourceManager.h"
#include <filesystem>
#include "fmt/base.h"
#include "fmt/format.h"
#include "platform/profiler/Profiler.h"
#include "engine/JobSystem.h"

class Profiler;

// Engine big brain
// Has direct control of resources and job system
struct EngineState {
public:
	void init();
	void loadAssets(Profiler& engineProfiler);
	void initRenderer(Profiler& engineProfiler);
	void renderFrame(Profiler& engineProfiler);
	void shutdown();

	std::filesystem::path getBasePath() const { return _basePath; }

	GPUResources& getGPUResources() { return _resources; }
	VkFence submitCommandBuffers(GPUQueue& queue);
private:
	const std::filesystem::path _basePath;
	GPUResources _resources;
};

// Controls and views the engines current global stage
// Multithreading needs staging but this can also function outside of threading
namespace EngineStages {
	inline std::atomic<uint32_t> currentFlags = ENGINE_STAGE_NONE;
	inline std::mutex stageMutex;
	inline std::condition_variable stageCV;

	inline const char* stageToString(EngineStage stage) {
		switch (stage) {
		case ENGINE_STAGE_NONE: return "NONE";
		case ENGINE_STAGE_LOADING_START: return "LOADING_START";
		case ENGINE_STAGE_LOADING_FILES_READY: return "LOADING_FILES_READY";
		case ENGINE_STAGE_LOADING_SAMPLERS_READY: return "LOADING_SAMPLERS_READY";
		case ENGINE_STAGE_LOADING_TEXTURES_READY: return "LOADING_TEXTURES_READY";
		case ENGINE_STAGE_LOADING_MATERIALS_READY: return "LOADING_MATERIALS_READY";
		case ENGINE_STAGE_LOADING_MESHES_READY: return "LOADING_MESHES_READY";
		case ENGINE_STAGE_MESH_UPLOAD_READY: return "MESH_UPLOAD_READY";
		case ENGINE_STAGE_LOADING_SCENE_GRAPH_READY: return "LOADING_SCENE_GRAPH_READY";

		case ENGINE_STAGE_RENDER_PREPARING_FRAME: return "RENDER_PREPARING_FRAME";
		case ENGINE_STAGE_RENDER_FRAME_CONTEXT_READY: return "RENDER_FRAME_CONTEXT_READY";
		case ENGINE_STAGE_RENDER_CAMERA_READY: return "RENDER_CAMERA_READY";
		case ENGINE_STAGE_RENDER_FRUSTUM_READY: return "RENDER_FRUSTUM_READY";
		case ENGINE_STAGE_RENDER_SCENE_READY: return "RENDER_SCENE_READY";
		case ENGINE_STAGE_RENDER_READY_TO_RENDER: return "RENDER_READY_TO_RENDER";
		case ENGINE_STAGE_RENDER_FRAME_IN_FLIGHT: return "RENDER_FRAME_IN_FLIGHT";

		case ENGINE_STAGE_READY: return "READY";
		case ENGINE_STAGE_SHUTDOWN: return "SHUTDOWN";
		case ENGINE_STAGE_SHUTDOWN_COMPLETE: return "SHUTDOWN_COMPLETE";
		default: return "UNKNOWN";
		}
	}

	inline std::string flagsToString(uint32_t flags) {
		std::string result;
		auto append = [&](EngineStage s) {
			if (flags & s) {
				if (!result.empty()) result += " | ";
				result += stageToString(s);
			}
		};

		append(ENGINE_STAGE_LOADING_START);
		append(ENGINE_STAGE_LOADING_FILES_READY);
		append(ENGINE_STAGE_LOADING_SAMPLERS_READY);
		append(ENGINE_STAGE_LOADING_TEXTURES_READY);
		append(ENGINE_STAGE_LOADING_MATERIALS_READY);
		append(ENGINE_STAGE_LOADING_MESHES_READY);
		append(ENGINE_STAGE_MESH_UPLOAD_READY);
		append(ENGINE_STAGE_LOADING_SCENE_GRAPH_READY);

		append(ENGINE_STAGE_RENDER_PREPARING_FRAME);
		append(ENGINE_STAGE_RENDER_FRAME_CONTEXT_READY);
		append(ENGINE_STAGE_RENDER_CAMERA_READY);
		append(ENGINE_STAGE_RENDER_FRUSTUM_READY);
		append(ENGINE_STAGE_RENDER_SCENE_READY);
		append(ENGINE_STAGE_RENDER_READY_TO_RENDER);
		append(ENGINE_STAGE_RENDER_FRAME_IN_FLIGHT);

		append(ENGINE_STAGE_READY);
		append(ENGINE_STAGE_SHUTDOWN);
		append(ENGINE_STAGE_SHUTDOWN_COMPLETE);

		if (result.empty()) result = "NONE";
		return result;
	}

	// The next stage to progress
	inline void SetGoal(EngineStage stage, uint32_t threadID = UINT32_MAX) {
		{
			std::lock_guard<std::mutex> lock(stageMutex);
			currentFlags.fetch_or(stage);
		}
		stageCV.notify_all();
		if (threadID == UINT32_MAX) {
			fmt::print("[EngineStage] Set: {} ({:032b})\n",
				flagsToString(currentFlags.load()),
				currentFlags.load());
		}
		else {
			JobSystem::log(threadID,
				fmt::format("[EngineStage] Set: {} ({:032b})\n",
				flagsToString(currentFlags.load()),
				currentFlags.load()));
		}
	}

	// deletes a stage
	// reset staging mid frame
	inline void Clear(EngineStage stage) {
		{
			std::lock_guard<std::mutex> lock(stageMutex);
			currentFlags.fetch_and(~stage);
		}
	}

	// useful conditional outside of submission
	// to ensure a job is done
	inline bool IsSet(EngineStage stage) {
		return currentFlags.load() & stage;
	}

	// like IsSet, but for more than one stage
	inline bool AllSet(uint32_t flags) {
		return (currentFlags.load() & flags) == flags;
	}

	// Won't start until a current stage is done
	inline void WaitUntil(EngineStage stage) {
		std::unique_lock<std::mutex> lock(stageMutex);
		stageCV.wait(lock, [&] { return IsSet(stage); });
	}
	// For more than one stage
	inline void WaitUntilAll(uint32_t flags) {
		std::unique_lock<std::mutex> lock(stageMutex);
		stageCV.wait(lock, [&] { return AllSet(flags); });
	}


	constexpr uint32_t loadingStageFlags =
		ENGINE_STAGE_LOADING_START |
		ENGINE_STAGE_LOADING_FILES_READY |
		ENGINE_STAGE_LOADING_SAMPLERS_READY |
		ENGINE_STAGE_LOADING_TEXTURES_READY |
		ENGINE_STAGE_LOADING_MATERIALS_READY |
		ENGINE_STAGE_LOADING_MESHES_READY |
		ENGINE_STAGE_LOADING_SCENE_GRAPH_READY;

	constexpr uint32_t renderFrameFlags =
		ENGINE_STAGE_RENDER_PREPARING_FRAME |
		ENGINE_STAGE_RENDER_FRAME_CONTEXT_READY |
		ENGINE_STAGE_RENDER_CAMERA_READY |
		ENGINE_STAGE_RENDER_FRUSTUM_READY |
		ENGINE_STAGE_RENDER_SCENE_READY |
		ENGINE_STAGE_RENDER_READY_TO_RENDER |
		ENGINE_STAGE_RENDER_FRAME_IN_FLIGHT;
}


namespace DeferredCmdSubmitQueue {
	inline std::mutex submitMutex;
	inline std::vector<VkCommandBuffer> recordedGraphicsCmds;
	inline std::vector<VkCommandBuffer> recordedTransferCmds;
	inline std::vector<VkCommandBuffer> recordedComputeCmds;

	inline void pushGraphics(VkCommandBuffer cmd) {
		std::scoped_lock lock(submitMutex);
		recordedGraphicsCmds.push_back(cmd);
	}
	inline void pushTransfer(VkCommandBuffer cmd) {
		std::scoped_lock lock(submitMutex);
		recordedTransferCmds.push_back(cmd);
	}
	inline void pushCompute(VkCommandBuffer cmd) {
		std::scoped_lock lock(submitMutex);
		recordedComputeCmds.push_back(cmd);
	}

	inline std::vector<VkCommandBuffer> collectGraphics() {
		std::scoped_lock lock(submitMutex);
		std::vector<VkCommandBuffer> collected = std::move(recordedGraphicsCmds);
		recordedGraphicsCmds.clear();
		return collected;
	}
	inline std::vector<VkCommandBuffer> collectTransfer() {
		std::scoped_lock lock(submitMutex);
		std::vector<VkCommandBuffer> collected = std::move(recordedTransferCmds);
		recordedTransferCmds.clear();
		return collected;
	}
	inline std::vector<VkCommandBuffer> collectCompute() {
		std::scoped_lock lock(submitMutex);
		std::vector<VkCommandBuffer> collected = std::move(recordedComputeCmds);
		recordedComputeCmds.clear();
		return collected;
	}
}