#pragma once

#include "renderer/gpu/PipelineBuilder.h"

namespace Pipelines {
	inline PipelineLayoutConst _globalLayout;

	// === GRAPHICS PIPELINES ===
	inline PipelineObj opaquePipeline;
	inline PipelineObj transparentPipeline;
	inline PipelineObj wireframePipeline;
	inline PipelineObj boundingBoxPipeline;
	inline PipelineObj skyboxPipeline;
	//inline PipelineObj shadowPipeline;

	// === COMPUTE PIPELINES ===
	inline PipelineObj visibilityPipeline;	// Culls GPURenderObjects
	//inline PipelineObj buildDrawsPipeline;
	//inline PipelineObj sortDrawsPipeline;

	inline PipelineObj postProcessPipeline;

	inline PipelineObj hdr2cubemapPipeline;
	inline PipelineObj specularPrefilterPipeline;
	inline PipelineObj diffuseIrradiancePipeline;
	inline PipelineObj brdfLutPipeline;
}

namespace PipelineManager {
	VkPipelineShaderStageCreateInfo setShader(const char* shaderFile, VkShaderStageFlagBits stage, DeletionQueue& shaderDeleteQueue);

	// only function needed outside of pipeline system
	VkPipelineShaderStageCreateInfo createPipelineShaderStage(VkShaderStageFlagBits stage, VkShaderModule shaderModule);
	VkPipelineLayout createPipelineLayout(const std::vector<VkDescriptorSetLayout>& setLayouts, const PushConstantDef pushConstants);

	void setupPipelineConfig(PipelineBuilder& pipeline, PipelinePresent& settings, bool msaaOn);
	void setupShaders(PipelinePresent& pipelineSettings, DeletionQueue& shaderDeletionQueue);

	// configurations for pipelines altered here
	void initPipelines(DeletionQueue& queue);
	void initShaders(DeletionQueue& queue);
	void definePipelineData();
}

namespace PipelineConfigs {
	// graphics pipeline
	void inputAssemblyConfig(VkPipelineInputAssemblyStateCreateInfo& inputAssembly, VkPrimitiveTopology topology, bool primitiveRestartEnabled);
	void rasterizerConfig(VkPipelineRasterizationStateCreateInfo& rasterizer, VkPolygonMode mode, float lineWidth, VkCullModeFlags cullMode, VkFrontFace frontFace);
	void multisamplingConfig(VkPipelineMultisampleStateCreateInfo& multisampling, const std::vector<VkSampleCountFlags>& samples,
		uint32_t chosenMSAACount, bool sampleShadingEnabled);
	void colorBlendingConfig(VkPipelineColorBlendAttachmentState& colorBlend, VkColorComponentFlags colorComponents, bool blendEnabled, VkBlendFactor blendFactor);
	void setColorAttachmentAndDepthFormat(VkFormat& colorAttachmentFormat, VkFormat colorFormat, VkPipelineRenderingCreateInfo& renderInfo, VkFormat depthFormat);
	void depthStencilConfig(VkPipelineDepthStencilStateCreateInfo& depthStencil, bool depthTestEnabled, bool depthWriteEnabled, bool depthBoundsTestEnabled, bool stencilTestEnabled, VkCompareOp depthCompare);
}