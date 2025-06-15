#include "pch.h"

#include "Engine.h"
#include "core/JobSystem.h"

static std::unique_ptr<Window> _window;
static std::unique_ptr<EngineState> engineState;

namespace Engine {
	GLFWwindow* getWindow() { return _window ? _window->window : nullptr; }
	// just returns the whole window struct for its use
	Window& windowModMode() { return *_window; }

	VkExtent2D _windowExtent = { 800, 800 };
	VkExtent2D& getWindowExtent() { return _windowExtent; }

	Profiler _engineProfiler;
	Profiler& getProfiler() { return _engineProfiler; }

	bool _isInitialized{ false };
	bool isInitialized() { return _isInitialized; }
	bool _stopRendering{ false };
	bool hasRenderStopped() { return _stopRendering; }

	float _lastFrameTime = 0.f;
	float& getLastFrameTime() { return _lastFrameTime; }

	void resetState();

	void cleanup();
}

EngineState& Engine::getState() {
	if (!engineState) engineState = std::make_unique<EngineState>();
	return *engineState;
}
void Engine::resetState() {
	engineState = std::make_unique<EngineState>();
	engineState->init();
}

void Engine::initWindow() {
	_window = std::make_unique<Window>();
	_window->initWindow(_windowExtent.width, _windowExtent.height);
}

void Engine::resetWindow() {
	if (_window) {
		_window->cleanupWindow();
	}
	if (_isInitialized == false) return; // shutdown

	_window = std::make_unique<Window>();
	_window->initWindow(_windowExtent.width, _windowExtent.height);
}


void Engine::run() {
	initWindow();
	Backend::initVulkanCore();

	getState().init();
	_isInitialized = true;

	getState().loadAssets();

	_lastFrameTime = static_cast<float>(glfwGetTime());

	//_engineProfiler.getStats().capFramerate = true;
	//_engineProfiler.getStats().targetFrameRate = TARGET_FRAME_RATE_120;

	// TODO: Fix visual frame data inconsistencies like fps,
	// look into any other profiler stats
	while (WindowIsOpen(_window->window)) {
		//if (EngineStages::IsSet(ENGINE_STAGE_SHUTDOWN)) break;

		_engineProfiler.beginFrame();
		_engineProfiler.updateDeltaTime(_lastFrameTime);
		glfwPollEvents();

		//EngineStages::WaitUntil(ENGINE_STAGE_RENDER_FRAME_IN_FLIGHT);
		//EngineStages::Clear(static_cast<EngineStage>(EngineStages::renderFrameFlags));

		//EngineStages::SetGoal(ENGINE_STAGE_READY);
		getState().renderFrame();

		_engineProfiler.endFrame();
		//_lastFrameTime = static_cast<float>(glfwGetTime());
	}
	JobSystem::wait();

	_stopRendering = true;

	//EngineStages::WaitUntil(ENGINE_STAGE_RENDER_FRAME_IN_FLIGHT);

	//if (!EngineStages::IsSet(ENGINE_STAGE_SHUTDOWN)) {
	//	EngineStages::SetGoal(ENGINE_STAGE_SHUTDOWN);
	//}

	cleanup();
}

void Engine::cleanup() {
	if (_isInitialized) {
		_isInitialized = false;
		vkDeviceWaitIdle(Backend::getDevice());

		getState().shutdown();

		Backend::cleanupBackend();

		resetWindow();
	}
}