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

	float _lastFrameTime = 0.0f;
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

	// frame capping is fucking busted
	_engineProfiler.getStats().capFramerate = false;

	while (WindowIsOpen(_window->window)) {
		//if (EngineStages::IsSet(ENGINE_STAGE_SHUTDOWN)) break;

		_engineProfiler.updateDeltaTime(_lastFrameTime);

		_engineProfiler.beginFrame();

		glfwPollEvents();

		//EngineStages::WaitUntil(ENGINE_STAGE_RENDER_FRAME_IN_FLIGHT);
		//EngineStages::Clear(static_cast<EngineStage>(EngineStages::renderFrameFlags));

		//EngineStages::SetGoal(ENGINE_STAGE_READY);

		getState().renderFrame();

		_engineProfiler.endFrame();
	}
	JobSystem::wait();

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