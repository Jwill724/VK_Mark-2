#include "pch.h"

#include "Window.h"
#include "engine/Engine.h"
#include "renderer/Renderer.h"

static void framebufferResizeCallback(GLFWwindow* window, int width, int height) {
	auto win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
}

bool WindowIsOpen(GLFWwindow* window) {
	return !(glfwWindowShouldClose(window));
}

bool Window::throttleIfWindowUnfocused(double sleepMs) const {
	if (!glfwGetWindowAttrib(window, GLFW_VISIBLE) || !glfwGetWindowAttrib(window, GLFW_FOCUSED)) {
		glfwWaitEventsTimeout(sleepMs);
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

	auto& mainWindow = Engine::getWindowExtent();

	// Ensures global window extent is up to date
	mainWindow.width = static_cast<uint32_t>(width);
	mainWindow.height = static_cast<uint32_t>(height);

	VkExtent3D newDrawExtent {
		mainWindow.width,
		mainWindow.height,
		1
	};

	Renderer::setDrawExtent(newDrawExtent);
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

	GLFWmonitor* mon = glfwGetPrimaryMonitor();
	const GLFWvidmode* vm = glfwGetVideoMode(mon);
	int mx = 0, my = 0;
	glfwGetMonitorPos(mon, &mx, &my);
	int x = mx + (vm->width - (int)width) / 2;
	int y = my + (vm->height - (int)height) / 2;
	glfwSetWindowPos(window, x, y);

	glfwSetWindowUserPointer(window, this);
	glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
}

void Window::pollEvents() const {
	glfwPollEvents();
}

void Window::cleanupWindow() const {
	glfwDestroyWindow(window);
	glfwTerminate();
}