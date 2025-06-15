#pragma once

#include "GLFW/glfw3.h"

bool WindowIsOpen(GLFWwindow* window);

struct Window {
	GLFWwindow* window = nullptr;
	bool windowResized = false;

	void updateWindowSize() const;

	void initWindow(const uint32_t width, const uint32_t height);
	void cleanupWindow() const;
};