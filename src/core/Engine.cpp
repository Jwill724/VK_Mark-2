#include "pch.h"

#include "Engine.h"
#include "Window.h"
#include "JobSystem.h"
#include "AssetManager.h"
#include "../profiler/EditorImgui.h"
#include "../input/UserInput.h"
#include "renderer/Renderer.h"
#include "renderer/backend/Device.h"
#include "renderer/backend/PipelineManager.h"
#include "renderer/backend/DescriptorManager.h"

namespace Engine
{
	Window _mainWindow;
	//const Window& GetWindow() { return _mainWindow; }

	Renderer _renderer;
	JobSystem _jobSystem;
	Editor _editor;
	AssetManager _assetManager;

	static constexpr uint32_t DEFAULT_WIN_EXTENT_W = 1280;
	static constexpr uint32_t DEFAULT_WIN_EXTENT_H = 960;

	bool _bIsInitialized{ false };
	bool IsInitialized() { return _bIsInitialized; }

	bool _bHasWindowResized{ false };

	void Cleanup();

	void Run();

	std::vector<SceneUploadBatch> _pendingBatches;
	std::mutex _batchMutex;
}

void Engine::Run()
{
	_mainWindow.Init(DEFAULT_WIN_EXTENT_W, DEFAULT_WIN_EXTENT_H, "Mark_3");

	_jobSystem.Init();

	_renderer.Init(
		_mainWindow,
		_jobSystem);

	_editor.InitImgui(_renderer, _mainWindow.GetWindowHandle());

	_bIsInitialized = true;

	_renderer.StartTimer();
	_assetManager.LoadScenes(
		[&](SceneUploadBatch&& batch)
		{
			std::scoped_lock lock(_batchMutex);
			_pendingBatches.push_back(std::move(batch));
		},
		_jobSystem);

	_jobSystem.Wait();

	_renderer.UploadScenes(std::move(_pendingBatches));
	_renderer.EndAssetTimer();
	_pendingBatches.clear();

	while (_mainWindow.IsOpen())
	{
		_mainWindow.PollEvents();

		if (_mainWindow.ConsumeResizeFlag() || _bHasWindowResized)
		{
			// Block on events instead of spinning, and keep the resize pending.
			if (_mainWindow.IsMinimized())
			{
				_bHasWindowResized = true;
				glfwWaitEvents();
				continue;
			}

			_renderer.StallDevice();
			_renderer.UpdateDrawExtentUsage(_mainWindow.GetExtent());

			// Drop this frame's mouse motion — the window just moved under it.
			UserInput::NotifyWindowResized();

			_bHasWindowResized = false;
		}

		if (_mainWindow.ThrottleIfWindowUnfocused(0.033)) continue;

		_renderer.BeginFrameTimer();

		_renderer.TickVramUsage();

		if (_renderer.ShouldRenderImgui())
		{
			_editor.RenderImgui(_renderer);
		}

		if (_renderer.PrepareFrame())
		{
			_bHasWindowResized = true;
			_renderer.EndFrameTimer();
			continue;
		}

		_renderer.StartTimer();
		_renderer.UpdateRendererContext(_mainWindow.GetWindowHandle());
		_renderer.EndSceneUpdateTimer();

		_renderer.StartTimer();
		_renderer.RecordRenderCommand(_jobSystem);
		_renderer.EndDrawTimer();

		// Handled at the top of the next iteration, after PollEvents.
		_bHasWindowResized = _renderer.SubmitFrame();

		_renderer.EndFrameTimer();
	}

	Cleanup();
}

void Engine::Cleanup()
{
	if (_bIsInitialized)
	{
		_bIsInitialized = false;
		_renderer.StallDevice();
		_assetManager.Shutdown(_jobSystem);
		_renderer.UnloadAllScenes();
		_editor.Shutdown(_renderer);
		_renderer.Cleanup();
		_jobSystem.Shutdown();
		_mainWindow.Cleanup();
	}
}
