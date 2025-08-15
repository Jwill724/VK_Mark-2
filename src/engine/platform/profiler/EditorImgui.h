#pragma once

#include "engine/Engine.h"

namespace EditorImgui {
	void initImgui(
		const VkDevice device,
		const VkPhysicalDevice pDevice,
		const VkQueue gQueue,
		const VkInstance instance,
		const VkFormat swapFormat,
		DeletionQueue& queue);
	void renderImgui(Profiler& profiler);
	void drawImgui(VkCommandBuffer cmd, VkImageView targetImageView, const VkExtent2D swapExtent, bool shouldClear);
}