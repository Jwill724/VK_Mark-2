#pragma once

#include "common/Vk_Types.h"
#include "vulkan/Window.h"

namespace Engine {
	GLFWwindow* getWindow();
	Window& windowModMode();

	VkExtent2D getWindowExtent();
	DeletionQueue& getDeletionQueue();
	VmaAllocator& getAllocator();

	bool isInitialized();
	bool hasRenderStopped();

	float& getLastTimeCount();

	// inits everything, controls runtime, and cleans up
	void run();
}