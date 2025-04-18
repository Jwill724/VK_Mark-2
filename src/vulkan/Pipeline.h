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

	VkPipeline createPipeline(PipelinePresent pipelineSettings);
};

struct ComputePipeline {
	ComputeEffect& getComputeEffect() { return compEffects[currEffect]; }
	int& getCurrentComputeEffect() { return currEffect; }

	VkPipelineLayout _computePipelineLayout = VK_NULL_HANDLE;

	// compute shader stuff
	std::vector<ComputeEffect> compEffects;
	int currEffect{ 0 };
};

struct ComputePipelineBuilder {
	std::vector<ComputeEffect> build(ComputePipeline& pipeline, PipelinePresent& settings);
};