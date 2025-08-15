#include "pch.h"

#include "Engine.h"
#include "renderer/Renderer.h"
#include "platform/profiler/Profiler.h"

static std::unique_ptr<Window> _window;
static std::unique_ptr<EngineState> engineState;

namespace Engine {
	GLFWwindow* getWindow() { return _window ? _window->window : nullptr; }
	// just returns the whole window struct for its use
	const Window& windowModMode() { return *_window; }

	VkExtent2D _windowExtent { 1280, 960 };
	VkExtent2D& getWindowExtent() { return _windowExtent; }

	Profiler _engineProfiler;
	Profiler& getProfiler() { return _engineProfiler; }

	bool _isInitialized{ false };
	bool isInitialized() { return _isInitialized; }

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

	getState().loadAssets(_engineProfiler);
	getState().initRenderer(_engineProfiler);

	_engineProfiler.getStats().capFramerate = true;
	_engineProfiler.getStats().targetFrameRate = TARGET_FRAME_RATE_240;

	while (WindowIsOpen(_window->window)) {
		_window->pollEvents();

		if (_window->throttleIfWindowUnfocused(0.033)) continue;

		_engineProfiler.beginFrame();

		getState().renderFrame(_engineProfiler);

		_engineProfiler.endFrame();
	}

	cleanup();
}

void Engine::cleanup() {
	if (_isInitialized) {
		_isInitialized = false;
		Backend::deviceIdle();

		getState().shutdown();

		Backend::cleanupBackend();

		resetWindow();
	}
}