#pragma once

#include "Pipeline.h"
#include "renderer/Descriptor.h"

namespace PipelineManager {
	// only function needed outside of pipeline system
	VkPipelineShaderStageCreateInfo createPipelineShaderStage(VkShaderStageFlagBits stage, VkShaderModule shaderModule);
	void createPipelineLayout(VkPipelineLayout& pipelineLayout, DescriptorsCentral& descriptors, const PushConstantDef* pushConstants);
	void setupPipelineLayouts();

	void setupGraphicsPipelineCofig(GraphicsPipeline& pipeline);
	void setupShaders(GraphicsPipeline& pipeline, std::vector<ShaderStageInfo>& shaderStageInfo, DeletionQueue& shaderDeletionQueue);

	// backend calls this
	void initPipelines();
}

namespace Pipelines {
	inline ComputePipeline drawImagePipeline;
	inline GraphicsPipeline meshPipeline;
}

namespace PipelineConfigs {
	// graphics pipeline
	void inputAssemblyConfig(VkPipelineInputAssemblyStateCreateInfo& inputAssembly, VkPrimitiveTopology topology, bool primitiveRestartEnabled);
	void rasterizerConfig(VkPipelineRasterizationStateCreateInfo& rasterizer, VkPolygonMode mode, float lineWidth, VkCullModeFlags cullMode, VkFrontFace frontFace);
	void multisamplingConfig(VkPipelineMultisampleStateCreateInfo& multisampling, const std::vector<VkSampleCountFlags>& samples,
		uint32_t chosenMSAACount, bool sampleShadingEnabled);
	void colorBlendingConfig(VkPipelineColorBlendAttachmentState& colorBlend, VkColorComponentFlags colorComponents, bool blendEnabled, VkBlendFactor blendFactor);
	void setColorAttachmentAndDepthFormat(VkFormat& colorAttachmentformat, VkFormat colorFormat, VkPipelineRenderingCreateInfo& renderInfo, VkFormat depthFormat);
	void depthStencilConfig(VkPipelineDepthStencilStateCreateInfo& depthStencil, bool depthTestEnabled, bool depthWriteEnabled, bool depthBoundsTestEnabled, bool stencilTestEnabled, VkCompareOp depthCompare);
}