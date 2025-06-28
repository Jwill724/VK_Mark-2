#pragma once

#include "common/Vk_Types.h"

enum class ResourceType {
	Buffer,
	Image
};

enum class ResourceAccess {
	Read,
	Write,
	ReadWrite
};

struct RenderGraphResource {
	std::string name;
	ResourceType type;

	VkBuffer buffer = VK_NULL_HANDLE;
	VkImage image = VK_NULL_HANDLE;

	ResourceAccess access;
	VkPipelineStageFlags2 stage;
	VkAccessFlags2 accessFlags;

	VkImageLayout layoutBefore = VK_IMAGE_LAYOUT_UNDEFINED;
	VkImageLayout layoutAfter = VK_IMAGE_LAYOUT_UNDEFINED;
};

struct RenderGraphPass {
	std::string name;

	std::vector<RenderGraphResource> reads;
	std::vector<RenderGraphResource> writes;

	std::function<void(VkCommandBuffer)> executeCmd;
	std::function<void(VkCommandBuffer cmd)> recordSecondary = nullptr;
	std::vector<VkCommandBuffer> secondaryCmds;
	bool useSecondary = false;
};

class RenderGraph {
public:
	void beginFrame();
	void addComputePass(const std::string& name, std::function<void(VkCommandBuffer)> func);
	void addGraphicsPass(const std::string& name, std::function<void(VkCommandBuffer)> func, bool useSecondary = false);
	void addBarrier(const std::string& name, VkPipelineStageFlags2 src, VkPipelineStageFlags2 dst, VkAccessFlags2 srcAccess, VkAccessFlags2 dstAccess);

	void executePasses(VkCommandBuffer primaryCmd, VkCommandPool secondaryPool);

private:
	std::vector<RenderGraphPass> passes;
};