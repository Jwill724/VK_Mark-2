#pragma once

#include "glfw/glfw3.h"

class Window {
public:
	void updateWindowSize() const;

	bool throttleIfWindowUnfocused(double sleepMs) const;

	void pollEvents() const;

	void initWindow(const uint32_t width, const uint32_t height);
	void cleanup() const;

	bool isOpen() const;

	GLFWwindow* getWindow() const { return window; }
private:
	GLFWwindow* window = nullptr;
};
