#pragma once

#include "common/Vk_Types.h"
#include "common/ResourceTypes.h"

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

struct ComputePipelineBuilder {
	std::vector<ComputeEffect> build(PipelinePresent settings);

	VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
};