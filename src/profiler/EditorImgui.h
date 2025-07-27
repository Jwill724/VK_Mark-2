#pragma once

#include "Engine.h"

namespace EditorImgui {
	void initImgui(DeletionQueue& queue);
	void renderImgui(Profiler& profiler);
	void drawImgui(VkCommandBuffer cmd, VkImageView targetImageView, bool shouldClear);
}