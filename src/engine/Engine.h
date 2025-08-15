#pragma once

#include "platform/window/Window.h"
#include "EngineState.h"

namespace Engine {
	EngineState& getState();

	GLFWwindow* getWindow();
	const Window& windowModMode();

	VkExtent2D& getWindowExtent();
	void initWindow();
	void resetWindow();

	Profiler& getProfiler();

	bool isInitialized();

	// inits everything, controls runtime, and cleans up
	void run();
}