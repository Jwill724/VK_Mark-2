#pragma once

#include "core/ResourceManager.h"
#include "renderer/frame/FrameContext.h"

// graphics and compute queues
enum class PassGoal { Draw, Dispatch };

struct DynamicRenderingDesc {
	bool enabled = false;
	std::vector<VkRenderingAttachmentInfo> colorAttachments;
	bool hasDepth = false;
	VkRenderingAttachmentInfo depthAttachment{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
	VkRenderingInfo renderingInfo{ VK_STRUCTURE_TYPE_RENDERING_INFO };
};

struct RenderPassContext {
	VkCommandBuffer cmd;
	FrameContext& frame;
	GPUResources& gpu;
	VkPipelineLayout globalLayout;
	VkDescriptorSet unifiedSet;
	VkDescriptorSet frameSet;
	VkExtent2D drawExtent;

	inline void bindGlobalSets() const {
		const VkDescriptorSet sets[2]{ unifiedSet, frameSet };

		vkCmdBindDescriptorSets(
			cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
			globalLayout, 0, 2, sets, 0, nullptr);

		vkCmdBindDescriptorSets(
			cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
			globalLayout, 0, 2, sets, 0, nullptr);
	}
};

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

	VkFormat imageFormat = VK_FORMAT_UNDEFINED;
};

struct RenderGraphPass {
	std::string name;

	std::vector<RenderGraphResource> reads;
	std::vector<RenderGraphResource> writes;

	PassGoal passGoal = PassGoal::Draw;
	QueueType queue = QueueType::Graphics;

	DynamicRenderingDesc dynamic;

	std::function<void(VkCommandBuffer)> executeCmd = nullptr;
	std::function<void(RenderPassContext&)> executeRich = nullptr;

	std::function<void(DescriptorWriter& writer,
		VkDescriptorSet unifiedSet,
		FrameContext& frame)> updateDescriptors = nullptr;

	// recorded secondaries are stored here for the collection phase
	std::vector<VkCommandBuffer> secondaryCmds;
	bool useSecondary = false; // set true for graphics passes that need secondaries
};

class RenderGraph {
public:
	struct FrameBindings {
		FrameContext* frame = nullptr;
		GPUResources* gpu = nullptr;
		VkPipelineLayout globalLayout = VK_NULL_HANDLE;
		VkDescriptorSet unifiedSet = VK_NULL_HANDLE;
		VkExtent2D drawExtent{};
	};

	void beginFrame(const FrameBindings& bindings);
	void addPass(RenderGraphPass pass);

	// Deferred secondaries than collect in primary
	void executePassesDeferred(
		VkCommandBuffer primaryGraphicsCmd,
		VkCommandPool graphicsSecondaryPool);

private:
	std::vector<RenderGraphPass> passes;

	// per-frame resource tracking
	struct TrackedImageState {
		VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
		bool initialized = false;
		VkFormat format = VK_FORMAT_UNDEFINED;
	};
	std::unordered_map<std::string, TrackedImageState> imageStates;

	void ensureImageState(VkCommandBuffer cmd, const RenderGraphResource& r);
	VkCommandBufferInheritanceInfo makeInheritanceInfo(const RenderGraphPass& pass);

	FrameBindings frameBindings{};
};