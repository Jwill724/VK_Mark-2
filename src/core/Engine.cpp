#include "pch.h"

#include "Engine.h"
#include "renderer/Renderer.h"
#include "platform/profiler/Profiler.h"

static std::unique_ptr<Window> _window;
static std::unique_ptr<EngineState> engineState;

namespace Engine
{
	GLFWwindow* GetWindow() { return _window ? _window->GetWindowHandle() : nullptr; }
	// just returns the whole window struct for its use
	const Window& WindowModMode() { return *_window; }

	//VkExtent2D _windowExtent{ 1920, 1080 };
	VkExtent2D _windowExtent{ 1280, 960 };
	VkExtent2D& GetWindowExtent() { return _windowExtent; }

	Profiler _engineProfiler;
	Profiler& GetProfiler() { return _engineProfiler; }

	bool _isInitialized{ false };
	bool IsInitialized() { return _isInitialized; }

	void ResetState();

	void Cleanup();
}

EngineState& Engine::GetState() {
	if (!engineState) engineState = std::make_unique<EngineState>();
	return *engineState;
}
void Engine::ResetState() {
	engineState = std::make_unique<EngineState>();
	engineState->Init();
}

void Engine::InitWindow() {
	_window = std::make_unique<Window>();
	_window->InitWindow(_windowExtent.width, _windowExtent.height);
}

void Engine::ResetWindow() {
	if (_window) {
		_window->Cleanup();
	}
	if (_isInitialized == false) return; // shutdown

	_window = std::make_unique<Window>();
	_window->InitWindow(_windowExtent.width, _windowExtent.height);
}

void Engine::Run() {
	InitWindow();
	Backend::InitVulkanCore();

	GetState().Init();
	_isInitialized = true;

	GetState().LoadAssets(_engineProfiler);
	GetState().InitRenderer(_engineProfiler);

	while (_window->IsOpen()) {
		_window->PollEvents();

		if (_window->ThrottleIfWindowUnfocused(0.033)) continue;

		_engineProfiler.beginFrame();

		GetState().RenderFrame(_engineProfiler);

		_engineProfiler.endFrame();
	}

	Cleanup();
}

void Engine::Cleanup() {
	if (_isInitialized) {
		_isInitialized = false;
		Backend::DeviceIdle();

		GetState().Shutdown();

		Backend::CleanupBackend();

		ResetWindow();
	}
}
