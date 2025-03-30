#include "Engine.h"
#include "vulkan/Backend.h"
#include "renderer/Renderer.h"
#include "imgui/EditorImgui.h"
#include "core/AssetManager.h"

namespace Engine {
	Window _window;
	GLFWwindow* getWindow() { return _window.window; }
	bool getWindowFramebufferResize() { return _window.framebufferResized; }
	void resetWindowFramebufferSize() { _window.framebufferResized = false; }

	DeletionQueue _mainDeletionQueue;
	VmaAllocator _allocator;

	DeletionQueue& getDeletionQueue() { return _mainDeletionQueue; }
	VmaAllocator& getAllocator() { return _allocator; }

	VkExtent2D _windowExtent = { 800, 600 };
	VkExtent2D getWindowExtent() { return _windowExtent; }

	bool _isInitialized{ false };
	bool isInitialized() { return _isInitialized; }
	bool stopRendering{ false };
	bool hasRenderStopped() { return stopRendering; }

	void init();
	void cleanup();

	void setupResources();
}

void Engine::setupResources() {
	Renderer::setMeshes(AssetManager::getTestMeshes());
}

void Engine::init() {
	_window.initWindow(_windowExtent.width, _windowExtent.height);
	Backend::initVulkan();

	AssetManager::loadAssets();
	setupResources();

	Backend::initBackend();

	_isInitialized = true;
}

void Engine::run() {
	init();
	Renderer::init();

	// main loop
	while (WindowIsOpen(_window.window)) {
		glfwPollEvents();

		glfwSetWindowFocusCallback(_window.window, EditorImgui::MyWindowFocusCallback);
		EditorImgui::renderImgui();
		Renderer::RenderFrame();
	}

	cleanup();
}

void Engine::cleanup() {
	if (_isInitialized) {
		_isInitialized = false;
		vkDeviceWaitIdle(Backend::getDevice());

		AssetManager::getAssetDeletionQueue().flush();

		_mainDeletionQueue.flush();

		Backend::cleanupBackend();
		_window.cleanupWindow();
	}
}