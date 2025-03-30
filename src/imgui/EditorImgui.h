#pragma once

#include "Engine.h"

namespace EditorImgui {
	void initImgui();
	void renderImgui();
	void drawImgui(VkCommandBuffer cmd, VkImageView targetImageView, bool shouldClear);
	void MyWindowFocusCallback(GLFWwindow* window, int focused);
}