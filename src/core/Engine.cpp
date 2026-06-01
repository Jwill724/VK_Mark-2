#include "pch.h"

#include "Engine.h"
#include "Window.h"
#include "JobSystem.h"
#include "../profiler/EditorImgui.h"
#include "renderer/Renderer.h"
#include "renderer/backend/Device.h"
#include "renderer/backend/PipelineManager.h"
#include "renderer/backend/DescriptorManager.h"

namespace Engine
{
	Window _mainWindow;
	const Window& GetWindow() { return _mainWindow; }

	Renderer _renderer;
	JobSystem _jobSystem;
	Editor _editor;

	static constexpr uint32_t DEFAULT_WIN_EXTENT_W = 1280;
	static constexpr uint32_t DEFAULT_WIN_EXTENT_H = 960;

	bool _isInitialized{ false };
	bool IsInitialized() { return _isInitialized; }

	void Cleanup();

	void Run();
}

void Engine::Run()
{
	_mainWindow.Init(DEFAULT_WIN_EXTENT_W, DEFAULT_WIN_EXTENT_H, "Mark_2.5");

	_jobSystem.Init();

	_renderer.Init(
		_mainWindow,
		_jobSystem);

	_editor.InitImgui(_renderer, _mainWindow.GetWindowHandle());

	_isInitialized = true;

	// Load assets

	bool hasWindowResized = false;

	while (_mainWindow.IsOpen())
	{
		_mainWindow.PollEvents();

		if (_mainWindow.ThrottleIfWindowUnfocused(0.033)) continue;

		_renderer.BeingFrameTimer();

		_renderer.TickVramUsage();

		if (_renderer.ShouldRenderImgui())
		{
			_editor.RenderImgui(_renderer);
		}

		hasWindowResized = _renderer.PrepareFrame();
		if (hasWindowResized)
		{
			_mainWindow.UpdateWindowSize();
			_renderer.UpdateDrawExtentUsage(_mainWindow.GetExtent());
			continue; // This condition shouldn't occur, but try again?
		}

		_renderer.ResetFrameStats();
		_renderer.StartTimer();
		_renderer.UpdateRendererContext(_mainWindow.GetWindowHandle());
		_renderer.EndSceneUpdateTimer();

		_renderer.StartTimer();
		_renderer.RecordRenderCommand();
		_renderer.EndDrawTimer();

		hasWindowResized = _renderer.SubmitFrame();
		if (hasWindowResized)
		{
			_mainWindow.UpdateWindowSize();
			_renderer.UpdateDrawExtentUsage(_mainWindow.GetExtent());
		}

		_renderer.EndFrameTimer();
	}

	Cleanup();
}

void Engine::Cleanup()
{
	if (_isInitialized)
	{
		_isInitialized = false;
		_renderer.StallDevice();
		_editor.Shutdown(_renderer);
		_renderer.Cleanup();
		_jobSystem.Shutdown();
		_mainWindow.Cleanup();
	}
}
