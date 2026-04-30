#pragma once

#include "engine/Engine.h"

namespace EditorImgui
{
	void InitImgui(DeletionQueue& queue);
	void renderImgui(Profiler& profiler);
	void drawImgui(
		VkCommandBuffer cmd,
		VkImageView targetImageView,
		const VkExtent2D swapExtent,
		bool shouldClear);

	enum class SettingsCategory : uint8_t {
		Render,
		Lighting,
		PostFX,
		Pipelines,
		Count
	};
}
