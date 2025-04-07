#pragma once

#include "common/Vk_Types.h"

// pipeline is now a creation tool
struct PipelineBuilder {

	PipelineBuilder() {
		initializePipelineSTypes();
	}

	VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;

	VkPipelineInputAssemblyStateCreateInfo _inputAssembly{};
	VkPipelineRasterizationStateCreateInfo _rasterizer{};
	VkPipelineColorBlendAttachmentState _colorBlendAttachment{};
	VkPipelineMultisampleStateCreateInfo _multisampling{};
	VkPipelineDepthStencilStateCreateInfo _depthStencil{};
	VkPipelineRenderingCreateInfo _renderInfo{};
	VkFormat _colorAttachmentformat{};

	void initializePipelineSTypes();

	VkPipeline createPipeline(PipelineConfigPresent pipelineConfig);
};

struct ComputePipeline {
	VkPipeline getComputePipeline() { return _computePipeline; }
	VkPipelineLayout& getComputePipelineLayout() { return _computePipelineLayout; }

	PipelineEffect& getBackgroundEffects() { return backgroundEffects[currentBackgroundEffect]; }
	int& getCurrentBackgroundEffect() { return currentBackgroundEffect; }

	VkPipeline _computePipeline = VK_NULL_HANDLE;
	VkPipelineLayout _computePipelineLayout = VK_NULL_HANDLE;

	// compute shader stuff
	std::vector<PipelineEffect> backgroundEffects;
	int currentBackgroundEffect{ 0 };

	// The pipelines available push constants
	PushConstantDef _pushConstantInfo{};

	// DescriptorSetOverwatch holds the descriptor information for our pipeline
	void createComputePipeline(DeletionQueue& deletionQueue);
};