#pragma once

#include "vulkan/Window.h"
#include "core/EngineState.h"

namespace Engine {
	EngineState& getState();

	GLFWwindow* getWindow();
	Window& windowModMode();

	VkExtent2D& getWindowExtent();
	void initWindow();
	void resetWindow();

	Profiler& getProfiler();

	bool isInitialized();

	// inits everything, controls runtime, and cleans up
	void run();
}