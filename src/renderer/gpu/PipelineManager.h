#pragma once

#include "renderer/gpu/PipelineBuilder.h"

enum class PipelineID : uint8_t {
	// === Base (graphics) ===
	Opaque,
	Transparent,
	Skybox,

	// Debug
	Wireframe,
	OBBLine,
	CascadeVPLine,

	// === Culling pipeline *inactive (compute) ===
	Visibility,

	// === Post-process ===
	ExposureReduce,   // compute
	ExposureFinalize, // compute
	ToneMap,          // graphics

	// === IBL (compute) ===
	HDRToCubemap,
	SpecularPrefilter,
	DiffuseIrradiance,
	BRDFLUT,

	// === DEPTH ===
	DepthPrepass,  // graphics
	ShadowCSM,     // graphics
	DepthPyramid,  // compute

	// === SSAO (compute) ===
	SSAO,
	SSAOBlur,

	// === GTAO (compute) ===
	GTAO,
	GTAOFilter,
	GTAOTemporalResolve,

	// === VOLUMETRIC LIGHTING (compute) ===
	VolumetricLight,
	VolumetricLightBlur,

	// === LENS FLARE (compute) ===
	FlareBright,
	FlareGen,

	Count
};

namespace Pipelines {
	// All pipelines shared the same layouts and push constant settings
	inline PipelineLayoutConst _globalLayout;

	inline std::array<PipelineHandle, static_cast<size_t>(PipelineID::Count)> _pipelineHandles;

	inline VkPipeline getPipeline(PipelineID id) {
		return _pipelineHandles[static_cast<size_t>(id)].pipeline;
	}
	inline PipelineHandle& getHandle(PipelineID id) {
		return _pipelineHandles[static_cast<size_t>(id)];
	}
	inline VkPrimitiveTopology getTopology(PipelineID id) {
		return _pipelineHandles[static_cast<size_t>(id)].topology;
	}

	inline std::vector<std::pair<PipelineID, PipelineHandle&>> getSwappablePipelines() {
		std::vector<std::pair<PipelineID, PipelineHandle&>> swappables;
		for (size_t i = 0; i < _pipelineHandles.size(); ++i) {
			if (_pipelineHandles[i].swappable) {
				swappables.emplace_back(static_cast<PipelineID>(i), _pipelineHandles[i]);
			}
		}
		return swappables;
	}
}

namespace PipelineManager {
	VkPipelineShaderStageCreateInfo setShader(const char* shaderFile, VkShaderStageFlagBits stage, DeletionQueue& shaderDeleteQueue);

	// only function needed outside of pipeline system
	VkPipelineShaderStageCreateInfo createPipelineShaderStage(VkShaderStageFlagBits stage, VkShaderModule shaderModule);
	VkPipelineLayout createPipelineLayout(const std::vector<VkDescriptorSetLayout>& setLayouts, const PushConstantDef pushConstants);

	void setupPipelineConfig(PipelineBuilder& pipeline, PipelinePreset& settings, bool msaaOn);
	void setupShaders(PipelinePreset& pipelineSettings, DeletionQueue& shaderDeletionQueue);

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
	void setColorAttachmentAndDepthFormat(std::vector<VkFormat>& colorFormats, VkPipelineRenderingCreateInfo& renderInfo, VkFormat depthFormat);
	void depthStencilConfig(VkPipelineDepthStencilStateCreateInfo& depthStencil, bool depthTestEnabled, bool depthWriteEnabled, bool depthBoundsTestEnabled, bool stencilTestEnabled, VkCompareOp depthCompare);
}