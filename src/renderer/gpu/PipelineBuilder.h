#pragma once

#include "common/ResourceTypes.h"
#include "common/EngineTypes.h"

// pipeline is now a creation tool
struct PipelineBuilder {
	VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;

	VkPipelineInputAssemblyStateCreateInfo _inputAssembly{};
	VkPipelineRasterizationStateCreateInfo _rasterizer{};
	VkPipelineColorBlendAttachmentState _colorBlendAttachment{};
	std::vector<VkPipelineColorBlendAttachmentState> _colorBlendAttachments;
	VkPipelineMultisampleStateCreateInfo _multisampling{};
	VkPipelineDepthStencilStateCreateInfo _depthStencil{};
	VkPipelineRenderingCreateInfo _renderInfo{};
	VkFormat _colorAttachmentformat{};

	std::vector<VkFormat> colorFormats;
	VkFormat depthFormat;

	uint32_t msaaCount = UINT32_MAX;

	void initializePipelineSTypes();

	void createPipeline(PipelineHandle& pipelineObj, const PipelinePreset& pipelineSettings, const VkDevice device);
};
