#include "pch.h"

#include "Window.h"
#include "Engine.h"
#include "renderer/Renderer.h"

static void framebufferResizeCallback(GLFWwindow* window, int width, int height) {
	auto win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
	win->windowResized = true;
}

bool WindowIsOpen(GLFWwindow* window) {
	return !(glfwWindowShouldClose(window));
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
}

void Window::initWindow(const uint32_t width, const uint32_t height) {

	assert(glfwInit() && "Failed to initialize GLFW!");

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

	window = glfwCreateWindow(width, height, "This is bullshit", nullptr, nullptr);

	//GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
	//const GLFWvidmode* mode = glfwGetVideoMode(primaryMonitor);

	//if (width == static_cast<uint32_t>(mode->width) && height == static_cast<uint32_t>(mode->height)) {
	//	// fullscreen
	//	window = glfwCreateWindow(width, height, "This is bullshit", primaryMonitor, nullptr);
	//}
	//else {
	//	// window
	//	window = glfwCreateWindow(width, height, "This is bullshit", nullptr, nullptr);
	//}

	glfwSetWindowUserPointer(window, this);
	glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
}

void Window::cleanupWindow() const {
	glfwDestroyWindow(window);
	glfwTerminate();
}