#pragma once

#include "common/ResourceTypes.h"
#include "common/EngineTypes.h"

// pipeline is now a creation tool
struct PipelineBuilder {
	VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;

	VkPipelineInputAssemblyStateCreateInfo _inputAssembly{};
	VkPipelineRasterizationStateCreateInfo _rasterizer{};
	VkPipelineColorBlendAttachmentState _colorBlendAttachment{};
	VkPipelineMultisampleStateCreateInfo _multisampling{};
	VkPipelineDepthStencilStateCreateInfo _depthStencil{};
	VkPipelineRenderingCreateInfo _renderInfo{};
	VkFormat _colorAttachmentformat{};

	VkFormat colorFormat;
	VkFormat depthFormat;

	void initializePipelineSTypes();

	void createPipeline(PipelineHandle& pipelineObj, const PipelinePresent& pipelineSettings, const VkDevice device);
};