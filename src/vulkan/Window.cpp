#include "Window.h"
#include <stdexcept>
#include <iostream>

static void framebufferResizeCallback(GLFWwindow* window, int width, int height) {
	auto win = reinterpret_cast<Window*>(glfwGetWindowUserPointer(window));
	win->framebufferResized = true;
}

bool WindowIsOpen(GLFWwindow* window) {
	return !(glfwWindowShouldClose(window));
}

void Window::initWindow(const uint32_t width, const uint32_t height) {
	if (!glfwInit()) {
		throw std::runtime_error("Failed to initialize GLFW!");
	}

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

	window = glfwCreateWindow(width, height, "This is bullshit", nullptr, nullptr);
	if (!window) {
		throw std::runtime_error("Failed to create window!");
	}

	glfwSetWindowUserPointer(window, this);
	glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
}

void Window::cleanupWindow() {
	glfwDestroyWindow(window);
	glfwTerminate();
}