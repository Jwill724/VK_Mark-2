#pragma once

#include "common/Vk_Types.h"
#include "Pipeline.h"

namespace Pipelines {
	inline GraphicsPipeline opaquePipeline;
	inline GraphicsPipeline transparentPipeline;
	inline GraphicsPipeline wireframePipeline;
	inline GraphicsPipeline boundingBoxPipeline;
	inline GraphicsPipeline skyboxPipeline;

	inline ComputePipeline postProcessPipeline;
	inline ComputePipeline hdr2cubemapPipeline;
}

namespace PipelinePresents {
	inline PipelinePresent metalRoughMatSettings;
	inline PipelinePresent opaqueSettings;
	inline PipelinePresent transparentSettings;
	inline PipelinePresent wireframeSettings;
	inline PipelinePresent boundingBoxSettings;
	inline PipelinePresent skyboxPipelineSettings;


	// === Compute pipeline setings ===
	// The primary use is for the push constant, descriptors, and shaderstages
	inline PipelinePresent colorCorrectionPipelineSettings;
	inline PipelinePresent hdr2cubemapPipelineSettings;
}

enum class PipelineType {
	Opaque,
	Transparent,
	Wireframe,
	BoundingBoxes
};

namespace PipelineManager {
	VkPipelineLayout setupPipelineLayout(PipelinePresent& pipelineInfo);
	VkPipelineShaderStageCreateInfo setShader(const char* shaderFile, VkShaderStageFlagBits stage, DeletionQueue& shaderDeleteQueue);

	// only function needed outside of pipeline system
	VkPipelineShaderStageCreateInfo createPipelineShaderStage(VkShaderStageFlagBits stage, VkShaderModule shaderModule);
	void createPipelineLayout(VkPipelineLayout& pipelineLayout, DescriptorsCentral& descriptors, const PushConstantDef* pushConstants);

	void setupPipelineConfig(PipelineBuilder& pipeline, PipelinePresent& settings);
	void setupShaders(std::vector<VkPipelineShaderStageCreateInfo>& shaderStages, std::vector<ShaderStageInfo>& shaderStageInfo, DeletionQueue& shaderDeletionQueue);

	// backend calls this
	void initPipelines();
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