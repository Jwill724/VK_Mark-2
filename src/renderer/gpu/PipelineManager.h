#pragma once

#include "common/Vk_Types.h"
#include "core/EngineState.h"
#include "vulkan/Pipeline.h"
#include "common/ResourceTypes.h"
#include "common/EngineTypes.h"

namespace Pipelines {
	inline PipelineLayoutConst _globalLayout;

	inline GraphicsPipeline opaquePipeline;
	inline GraphicsPipeline transparentPipeline;
	inline GraphicsPipeline wireframePipeline;
	inline GraphicsPipeline boundingBoxPipeline;
	inline GraphicsPipeline skyboxPipeline;
	inline GraphicsPipeline shadowPipeline;

	inline ComputePipeline postProcessPipeline;

	// Environment stuff
	inline ComputePipeline hdr2cubemapPipeline;
	inline ComputePipeline specularPrefilterPipeline;
	inline ComputePipeline diffuseIrradiancePipeline;
	inline ComputePipeline brdfLutPipeline;
}

namespace PipelineManager {
	VkPipelineShaderStageCreateInfo setShader(const char* shaderFile, VkShaderStageFlagBits stage, DeletionQueue& shaderDeleteQueue);

	// only function needed outside of pipeline system
	VkPipelineShaderStageCreateInfo createPipelineShaderStage(VkShaderStageFlagBits stage, VkShaderModule shaderModule);
	VkPipelineLayout createPipelineLayout(const std::vector<VkDescriptorSetLayout>& setLayouts, const PushConstantDef pushConstants);

	void setupPipelineConfig(PipelineBuilder& pipeline, PipelinePresent& settings);
	void setupShaders(std::vector<VkPipelineShaderStageCreateInfo>& shaderStages, std::vector<ShaderStageInfo>& shaderStageInfo, DeletionQueue& shaderDeletionQueue);

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