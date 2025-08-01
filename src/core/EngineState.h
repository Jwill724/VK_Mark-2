#pragma once

#include "renderer/gpu_types/PipelineManager.h"
#include "utils/RendererUtils.h"
#include "ResourceManager.h"
#include "AssetManager.h"
#include <filesystem>
#include "fmt/base.h"
#include "vulkan/Backend.h"
#include "profiler/Profiler.h"

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

	// The next stage to progress
	inline void SetGoal(EngineStage stage) {
		currentFlags.fetch_or(stage);
		fmt::print("[EngineStage] Set: {:032b}\n", static_cast<uint32_t>(stage));
	}

	// deletes a stage
	// reset staging mid frame
	inline void Clear(EngineStage stage) {
		currentFlags.fetch_and(~stage);
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
		while (!IsSet(stage)) {
			fmt::print("[EngineStage] Waiting for: {:032b}\n", static_cast<uint32_t>(stage));
			std::this_thread::yield();
		}
	}
	// For more than one stage
	inline void WaitUntilAll(uint32_t flags) {
		while (!AllSet(flags)) {
			fmt::print("[EngineStage] Waiting for: {:032b}\n", static_cast<uint32_t>(flags));
			std::this_thread::yield();
		}
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