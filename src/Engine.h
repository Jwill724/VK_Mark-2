#pragma once

#include "vulkan/Window.h"
#include "core/EngineState.h"
#include "profiler/Profiler.h"

namespace Engine {
	EngineState& getState();

	GLFWwindow* getWindow();
	Window& windowModMode();

	VkExtent2D& getWindowExtent();
	void initWindow();
	void resetWindow();

	Profiler& getProfiler();

	bool isInitialized();

	float& getLastFrameTime();

	// inits everything, controls runtime, and cleans up
	void run();
}