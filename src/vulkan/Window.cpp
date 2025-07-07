#include "pch.h"

#include "Window.h"
#include "Engine.h"
#include "renderer/Renderer.h"

static void framebufferResizeCallback(GLFWwindow* window, int width, int height) {
	auto win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
}

bool WindowIsOpen(GLFWwindow* window) {
	return !(glfwWindowShouldClose(window));
}

bool Window::throttleIfWindowUnfocused(int sleepMs) const {
	if (!glfwGetWindowAttrib(window, GLFW_VISIBLE) || !glfwGetWindowAttrib(window, GLFW_FOCUSED)) {
		std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
		Engine::getProfiler().resetFrameTimer();
		return true;
	}
	return false;
}

void Window::updateWindowSize() const {
	int width = 0, height = 0;
	glfwGetFramebufferSize(window, &width, &height);
	while (width == 0 || height == 0) {
		glfwGetFramebufferSize(window, &width, &height);
		glfwWaitEvents();
	}

	// Ensures global window extent is up to date
	Engine::getWindowExtent().width = static_cast<uint32_t>(width);
	Engine::getWindowExtent().height = static_cast<uint32_t>(height);

	VkExtent3D newDrawExtent = {
		Engine::getWindowExtent().width,
		Engine::getWindowExtent().height,
		1
	};

	Renderer::setDrawExtent(newDrawExtent);

	Engine::getProfiler().resetRenderTimers();
}

void Window::initWindow(const uint32_t width, const uint32_t height) {

	int glfwResult = glfwInit();
	if (!glfwResult) {
		ASSERT(glfwResult && "Failed to initialize GLFW!");
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

	window = glfwCreateWindow(width, height, "Mark 2", nullptr, nullptr);
	if (!window) {
		ASSERT(window && "Failed to initialize GLFW window!");
	}

	glfwSetWindowUserPointer(window, this);
	glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
}

void Window::cleanupWindow() const {
	glfwDestroyWindow(window);
	glfwTerminate();
}