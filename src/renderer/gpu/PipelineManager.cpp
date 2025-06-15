#include "pch.h"

#include "PipelineManager.h"

#include "vulkan/Backend.h"
#include "renderer/Renderer.h"
#include "renderer/RenderScene.h"
#include "core/ResourceManager.h"
#include "renderer/gpu/Descriptor.h"
#include "common/EngineConstants.h"

namespace PipelinePresents {
	PipelinePresent opaqueSettings;
	PipelinePresent transparentSettings;
	PipelinePresent wireframeSettings;
	PipelinePresent boundingBoxSettings;
	PipelinePresent skyboxPipelineSettings;
	PipelinePresent shadowPipelineSettings;


	// === Compute pipeline setings ===
	// The primary use is for the push constant, descriptors, and shaderstages
	PipelinePresent colorCorrectionPipelineSettings;

	// Environment stuff
	PipelinePresent hdr2cubemapPipelineSettings;
	PipelinePresent specularPrefilterPipelineSettings;
	PipelinePresent diffuseIrradiancePipelineSettings;
	PipelinePresent brdfLutPipelineSettings;
}

void PipelineManager::initShaders(DeletionQueue& dq) {
	// === GRAPHIC PIPELINES ===

	std::vector<ShaderStageInfo> meshShaderStages;
	ShaderStageInfo vertexStage = {
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.filePath = "res/shaders/meshes/mesh_vert.spv"
	};
	ShaderStageInfo fragmentStage = {
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		.filePath = "res/shaders/meshes/mesh_frag.spv"
	};

	meshShaderStages.push_back(vertexStage);
	meshShaderStages.push_back(fragmentStage);

	setupShaders(PipelinePresents::opaqueSettings.shaderStages, meshShaderStages, dq);
	setupShaders(PipelinePresents::transparentSettings.shaderStages, meshShaderStages, dq);
	setupShaders(PipelinePresents::wireframeSettings.shaderStages, meshShaderStages, dq);


	// separate shaders needed for bounding boxes
	ShaderStageInfo bbVertStage = {
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.filePath = "res/shaders/bounding_boxes/aabb_vert.spv"
	};
	ShaderStageInfo bbFragStage = {
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		.filePath = "res/shaders/bounding_boxes/aabb_frag.spv"
	};
	PipelinePresents::boundingBoxSettings.shaderStagesInfo.push_back(bbVertStage);
	PipelinePresents::boundingBoxSettings.shaderStagesInfo.push_back(bbFragStage);

	setupShaders(PipelinePresents::boundingBoxSettings.shaderStages, PipelinePresents::boundingBoxSettings.shaderStagesInfo, dq);


	ShaderStageInfo skyboxVertStage = {
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.filePath = "res/shaders/environment/skybox_vert.spv"
	};

	ShaderStageInfo skyboxFragStage = {
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		.filePath = "res/shaders/environment/skybox_frag.spv"
	};
	PipelinePresents::skyboxPipelineSettings.shaderStagesInfo.push_back(skyboxVertStage);
	PipelinePresents::skyboxPipelineSettings.shaderStagesInfo.push_back(skyboxFragStage);

	setupShaders(PipelinePresents::skyboxPipelineSettings.shaderStages, PipelinePresents::skyboxPipelineSettings.shaderStagesInfo, dq);

	// === COMPUTE PIPELINES ===

	ShaderStageInfo colorCorrectShaderStage = {
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.shaderName = "Color Correction",
		.filePath = "res/shaders/post_process/color_correction_comp.spv"
	};
	PipelinePresents::colorCorrectionPipelineSettings.shaderStagesInfo.push_back(colorCorrectShaderStage);

	setupShaders(PipelinePresents::colorCorrectionPipelineSettings.shaderStages,
		PipelinePresents::colorCorrectionPipelineSettings.shaderStagesInfo, dq);


	ShaderStageInfo cubemapShaderStage = {
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.shaderName = "cubemap",
		.filePath = "res/shaders/environment/hdr2cubemap_comp.spv"
	};
	PipelinePresents::hdr2cubemapPipelineSettings.shaderStagesInfo.push_back(cubemapShaderStage);

	setupShaders(PipelinePresents::hdr2cubemapPipelineSettings.shaderStages,
		PipelinePresents::hdr2cubemapPipelineSettings.shaderStagesInfo, dq);

	ShaderStageInfo prefilterShaderStage = {
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.shaderName = "specular_prefilter",
		.filePath = "res/shaders/environment/specular_prefilter_comp.spv"
	};

	PipelinePresents::specularPrefilterPipelineSettings.shaderStagesInfo.push_back(prefilterShaderStage);

	setupShaders(PipelinePresents::specularPrefilterPipelineSettings.shaderStages,
		PipelinePresents::specularPrefilterPipelineSettings.shaderStagesInfo, dq);


	ShaderStageInfo diffuseShaderStage = {
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.shaderName = "diffuse_irradiance",
		.filePath = "res/shaders/environment/diffuse_irradiance_comp.spv"
	};
	PipelinePresents::diffuseIrradiancePipelineSettings.shaderStagesInfo.push_back(diffuseShaderStage);

	setupShaders(PipelinePresents::diffuseIrradiancePipelineSettings.shaderStages,
		PipelinePresents::diffuseIrradiancePipelineSettings.shaderStagesInfo, dq);


	ShaderStageInfo brdfLutShaderStage = {
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.shaderName = "brdf_lut",
		.filePath = "res/shaders/environment/brdf_lut_comp.spv"
	};
	PipelinePresents::brdfLutPipelineSettings.shaderStagesInfo.push_back(brdfLutShaderStage);

	setupShaders(PipelinePresents::brdfLutPipelineSettings.shaderStages,
		PipelinePresents::brdfLutPipelineSettings.shaderStagesInfo, dq);
}

// defines push constants, descriptors, and pipeline layout
void PipelineManager::definePipelineData() {

	// === GRAPHICS PIPELINE DEFAULT ===
	uint32_t maxPC = Backend::getDeviceLimits().maxPushConstantsSize;
	assert(maxPC >= MAX_PUSH_CONSTANT_SIZE);
	maxPC = MAX_PUSH_CONSTANT_SIZE;

	PushConstantDef pcRange = {
		.offset = 0,
		.size = maxPC,
		.stageFlags = VK_SHADER_STAGE_ALL
	};

	fmt::print("Device max push constant size: {}\n", maxPC);

	auto setLayouts = {
		DescriptorSetOverwatch::getUnifiedDescriptors().descriptorLayout,
		DescriptorSetOverwatch::getFrameDescriptors().descriptorLayout
	};

	Pipelines::_globalLayout.layout = PipelineManager::createPipelineLayout(setLayouts, pcRange);
	Pipelines::_globalLayout.pcRange = pcRange;
}

void PipelineManager::initPipelines(DeletionQueue& queue) {
	DeletionQueue shaderDeletionQ;

	initShaders(shaderDeletionQ);

	definePipelineData();

	// graphic pipelines can share the same builder
	PipelineBuilder pipeline_builder;
	// reset back to default layout for the rest
	pipeline_builder._pipelineLayout = Pipelines::_globalLayout.layout;

	// === OPAQUE PIPELINE ===
	PipelinePresents::opaqueSettings.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	PipelinePresents::opaqueSettings.polygonMode = VK_POLYGON_MODE_FILL;
	PipelinePresents::opaqueSettings.cullMode = VK_CULL_MODE_NONE;
	PipelinePresents::opaqueSettings.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	PipelinePresents::opaqueSettings.enableBlending = false;
	PipelinePresents::opaqueSettings.enableDepthTest = true;
	PipelinePresents::opaqueSettings.enableDepthWrite = true;
	PipelinePresents::opaqueSettings.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	PipelinePresents::opaqueSettings.colorFormat = ResourceManager::getDrawImage().imageFormat;
	PipelinePresents::opaqueSettings.depthFormat = ResourceManager::getDepthImage().imageFormat;

	PipelineManager::setupPipelineConfig(pipeline_builder, PipelinePresents::opaqueSettings);
	Pipelines::opaquePipeline.pipeline = pipeline_builder.createPipeline(PipelinePresents::opaqueSettings);

	// === TRANSPARENT PIPELINE ===
	pipeline_builder.initializePipelineSTypes();
	PipelinePresents::transparentSettings.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	PipelinePresents::transparentSettings.polygonMode = VK_POLYGON_MODE_FILL;
	PipelinePresents::transparentSettings.cullMode = VK_CULL_MODE_NONE;
	PipelinePresents::transparentSettings.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	PipelinePresents::transparentSettings.enableBlending = true;
	PipelinePresents::transparentSettings.enableDepthTest = true;
	PipelinePresents::transparentSettings.enableDepthWrite = false;
	PipelinePresents::transparentSettings.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	PipelinePresents::transparentSettings.colorFormat = ResourceManager::getDrawImage().imageFormat;
	PipelinePresents::transparentSettings.depthFormat = ResourceManager::getDepthImage().imageFormat;

	PipelineManager::setupPipelineConfig(pipeline_builder, PipelinePresents::transparentSettings);
	Pipelines::transparentPipeline.pipeline = pipeline_builder.createPipeline(PipelinePresents::transparentSettings);


	// === WIREFRAME PIPELINE ===
	pipeline_builder.initializePipelineSTypes();
	PipelinePresents::wireframeSettings.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	PipelinePresents::wireframeSettings.polygonMode = VK_POLYGON_MODE_LINE;
	PipelinePresents::wireframeSettings.cullMode = VK_CULL_MODE_NONE;
	PipelinePresents::wireframeSettings.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	PipelinePresents::wireframeSettings.enableBlending = false;
	PipelinePresents::wireframeSettings.enableDepthTest = true;
	PipelinePresents::wireframeSettings.enableDepthWrite = true;
	PipelinePresents::wireframeSettings.depthCompareOp = VK_COMPARE_OP_LESS;
	PipelinePresents::wireframeSettings.colorFormat = ResourceManager::getDrawImage().imageFormat;
	PipelinePresents::wireframeSettings.depthFormat = ResourceManager::getDepthImage().imageFormat;

	PipelineManager::setupPipelineConfig(pipeline_builder, PipelinePresents::wireframeSettings);
	Pipelines::wireframePipeline.pipeline = pipeline_builder.createPipeline(PipelinePresents::wireframeSettings);


	// === BOUNDINGBOX PIPELINE ===
	pipeline_builder.initializePipelineSTypes();
	PipelinePresents::boundingBoxSettings.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	PipelinePresents::boundingBoxSettings.polygonMode = VK_POLYGON_MODE_LINE;
	PipelinePresents::boundingBoxSettings.cullMode = VK_CULL_MODE_NONE;
	PipelinePresents::boundingBoxSettings.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	PipelinePresents::boundingBoxSettings.enableBlending = false;
	PipelinePresents::boundingBoxSettings.enableDepthTest = true;
	PipelinePresents::boundingBoxSettings.enableDepthWrite = false;
	PipelinePresents::boundingBoxSettings.depthCompareOp = VK_COMPARE_OP_LESS;
	PipelinePresents::boundingBoxSettings.colorFormat = ResourceManager::getDrawImage().imageFormat;
	PipelinePresents::boundingBoxSettings.depthFormat = ResourceManager::getDepthImage().imageFormat;

	PipelineManager::setupPipelineConfig(pipeline_builder, PipelinePresents::boundingBoxSettings);
	Pipelines::boundingBoxPipeline.pipeline = pipeline_builder.createPipeline(PipelinePresents::boundingBoxSettings);


	// === SKYBOX PIPELINE ===
	pipeline_builder.initializePipelineSTypes();
	PipelinePresents::skyboxPipelineSettings.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	PipelinePresents::skyboxPipelineSettings.polygonMode = VK_POLYGON_MODE_FILL;
	PipelinePresents::skyboxPipelineSettings.cullMode = VK_CULL_MODE_NONE;
	PipelinePresents::skyboxPipelineSettings.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	PipelinePresents::skyboxPipelineSettings.enableBlending = false;
	PipelinePresents::skyboxPipelineSettings.enableDepthTest = true;
	PipelinePresents::skyboxPipelineSettings.enableDepthWrite = false;
	PipelinePresents::skyboxPipelineSettings.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	PipelinePresents::skyboxPipelineSettings.colorFormat = ResourceManager::getDrawImage().imageFormat;
	PipelinePresents::skyboxPipelineSettings.depthFormat = ResourceManager::getDepthImage().imageFormat;

	PipelineManager::setupPipelineConfig(pipeline_builder, PipelinePresents::skyboxPipelineSettings);
	Pipelines::skyboxPipeline.pipeline = pipeline_builder.createPipeline(PipelinePresents::skyboxPipelineSettings);

	// === COMPUTE PIPELINE SETUP STAGE ===

	ComputePipelineBuilder computeBuilder;
	computeBuilder._pipelineLayout = Pipelines::_globalLayout.layout;

	Pipelines::postProcessPipeline.computeEffects = computeBuilder.build(PipelinePresents::colorCorrectionPipelineSettings);

	// Cube map pipeline/layout will be deleted during the mapping function in AssetManager
	Pipelines::hdr2cubemapPipeline.computeEffects = computeBuilder.build(PipelinePresents::hdr2cubemapPipelineSettings);

	Pipelines::specularPrefilterPipeline.computeEffects = computeBuilder.build(PipelinePresents::specularPrefilterPipelineSettings);

	Pipelines::diffuseIrradiancePipeline.computeEffects = computeBuilder.build( PipelinePresents::diffuseIrradiancePipelineSettings);

	Pipelines::brdfLutPipeline.computeEffects = computeBuilder.build(PipelinePresents::brdfLutPipelineSettings);

	shaderDeletionQ.flush(); // deferred deletion of shader modules

	auto device = Backend::getDevice();
	queue.push_function([=] {
		for (auto& effect : Pipelines::postProcessPipeline.computeEffects) {
			vkDestroyPipeline(device, effect.pipeline, nullptr);
		}
		for (auto& effect : Pipelines::hdr2cubemapPipeline.computeEffects) {
			vkDestroyPipeline(device, effect.pipeline, nullptr);
		}
		for (auto& effect : Pipelines::specularPrefilterPipeline.computeEffects) {
			vkDestroyPipeline(device, effect.pipeline, nullptr);
		}
		for (auto& effect : Pipelines::diffuseIrradiancePipeline.computeEffects) {
			vkDestroyPipeline(device, effect.pipeline, nullptr);
		}
		for (auto& effect : Pipelines::brdfLutPipeline.computeEffects) {
			vkDestroyPipeline(device, effect.pipeline, nullptr);
		}

		vkDestroyPipeline(device, Pipelines::skyboxPipeline.pipeline, nullptr);
		vkDestroyPipeline(device, Pipelines::boundingBoxPipeline.pipeline, nullptr);
		vkDestroyPipeline(device, Pipelines::transparentPipeline.pipeline, nullptr);
		vkDestroyPipeline(device, Pipelines::wireframePipeline.pipeline, nullptr);
		vkDestroyPipeline(device, Pipelines::opaquePipeline.pipeline, nullptr);

		vkDestroyPipelineLayout(device, Pipelines::_globalLayout.layout, nullptr);
	});
}

// THE PIPELINE MANAGER
VkPipelineLayout PipelineManager::createPipelineLayout(const std::vector<VkDescriptorSetLayout>& setLayouts, const PushConstantDef pushConstants) {
	VkPipelineLayout pipelineLayout;

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.flags = 0;
	pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
	pipelineLayoutInfo.pSetLayouts = setLayouts.data();

	VkPushConstantRange pushConstantRange{};
	pushConstantRange.stageFlags = pushConstants.stageFlags;
	pushConstantRange.offset = pushConstants.offset;
	pushConstantRange.size = pushConstants.size;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

	VK_CHECK(vkCreatePipelineLayout(Backend::getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout));

	return pipelineLayout;
}

VkPipelineShaderStageCreateInfo PipelineManager::createPipelineShaderStage(VkShaderStageFlagBits stage, VkShaderModule shaderModule) {
	VkPipelineShaderStageCreateInfo shaderStageInfo{};
	shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageInfo.stage = stage;
	shaderStageInfo.module = shaderModule;
	shaderStageInfo.pName = "main";

	return shaderStageInfo;
}

void PipelineManager::setupShaders(std::vector<VkPipelineShaderStageCreateInfo>& shaderStages, std::vector<ShaderStageInfo>& shaderStageInfo, DeletionQueue& shaderDeletionQueue) {
	shaderStages.clear();

	for (auto& shaders : shaderStageInfo) {
		VkPipelineShaderStageCreateInfo shader = setShader(shaders.filePath, shaders.stage, shaderDeletionQueue);
		shaderStages.push_back(shader);
	}
}

VkPipelineShaderStageCreateInfo PipelineManager::setShader(const char* shaderFile, VkShaderStageFlagBits stage, DeletionQueue& shaderDeleteQueue) {
	VkShaderModule shaderModule;
	VulkanUtils::loadShaderModule(shaderFile, Backend::getDevice(), &shaderModule);
	VkPipelineShaderStageCreateInfo shaderStage = createPipelineShaderStage(stage, shaderModule);

	shaderDeleteQueue.push_function([=] {
		vkDestroyShaderModule(Backend::getDevice(), shaderModule, nullptr);
	});

	return shaderStage;
}

void PipelineManager::setupPipelineConfig(PipelineBuilder& pipeline, PipelinePresent& settings) {

	PipelineConfigs::inputAssemblyConfig(pipeline._inputAssembly, settings.topology, VK_FALSE);

	PipelineConfigs::rasterizerConfig(pipeline._rasterizer, settings.polygonMode, 1.f, settings.cullMode, settings.frontFace);

	PipelineConfigs::multisamplingConfig(pipeline._multisampling, ResourceManager::getAvailableSampleCounts(), MSAACOUNT, VK_FALSE);

	PipelineConfigs::colorBlendingConfig(pipeline._colorBlendAttachment,
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT, settings.enableBlending, VK_BLEND_FACTOR_ONE);

	if (settings.enableDepthTest && settings.enableDepthWrite) {
		PipelineConfigs::depthStencilConfig(pipeline._depthStencil, VK_TRUE, VK_TRUE, VK_FALSE, VK_FALSE, settings.depthCompareOp);
	}
	else if (settings.enableDepthTest && !settings.enableDepthWrite) {
		PipelineConfigs::depthStencilConfig(pipeline._depthStencil, VK_TRUE, VK_FALSE, VK_FALSE, VK_FALSE, settings.depthCompareOp);
	}
	else {
		PipelineConfigs::depthStencilConfig(pipeline._depthStencil, VK_FALSE, VK_FALSE, VK_FALSE, VK_FALSE, settings.depthCompareOp);
	}

	PipelineConfigs::setColorAttachmentAndDepthFormat(pipeline._colorAttachmentformat,
		settings.colorFormat, pipeline._renderInfo, settings.depthFormat);
}

// PIPELINE CONFIGURATION

void PipelineConfigs::inputAssemblyConfig(VkPipelineInputAssemblyStateCreateInfo& inputAssembly, VkPrimitiveTopology topology, bool primitiveRestartEnabled) {
	inputAssembly.topology = topology;
	inputAssembly.primitiveRestartEnable = primitiveRestartEnabled;
}
void PipelineConfigs::rasterizerConfig(VkPipelineRasterizationStateCreateInfo& rasterizer, VkPolygonMode mode, float lineWidth, VkCullModeFlags cullMode, VkFrontFace frontFace) {
	rasterizer.polygonMode = mode;
	rasterizer.lineWidth = lineWidth;
	rasterizer.cullMode = cullMode;
	rasterizer.frontFace = frontFace;
}

// 1u, 2u, 4u, 8u, 16u, 32u, 64u for msaa counts
// Even though max is 8 as of now
// FIXME: assumes every image needs msaa
void PipelineConfigs::multisamplingConfig(VkPipelineMultisampleStateCreateInfo& multisampling,
	const std::vector<VkSampleCountFlags>& samples, uint32_t chosenMSAACount, bool sampleShadingEnabled) {

	assert((chosenMSAACount != 0) && ((chosenMSAACount & (chosenMSAACount - 1)) == 0) && "Invalid MSAA count! Must be a power of two up to 64.");
	assert(chosenMSAACount <= 64 && "Invalid MSAA count! Must be a power of two up to 64.");

	// Default to sample count 1
	VkSampleCountFlagBits msaaSample = VK_SAMPLE_COUNT_1_BIT;
	if (static_cast<VkSampleCountFlagBits>(chosenMSAACount) == msaaSample) {
		multisampling.rasterizationSamples = msaaSample;
	}
	else {
		bool found = false;
		for (auto sample : samples) {
			if (sample == static_cast<VkSampleCountFlags>(chosenMSAACount)) {
				msaaSample = static_cast<VkSampleCountFlagBits>(sample);
				multisampling.rasterizationSamples = msaaSample;
				found = true;
				break;
			}
		}
		assert(found && "Failed to find valid MSAA sample count!");
	}

	multisampling.sampleShadingEnable = sampleShadingEnabled;
	multisampling.minSampleShading = 1.0f;
	multisampling.pSampleMask = nullptr;
	// no alpha to coverage either
	multisampling.alphaToCoverageEnable = VK_FALSE;
	multisampling.alphaToOneEnable = VK_FALSE;
}

void PipelineConfigs::colorBlendingConfig(VkPipelineColorBlendAttachmentState& colorBlend, VkColorComponentFlags colorComponents, bool blendEnabled, VkBlendFactor blendFactor) {
	colorBlend.colorWriteMask = colorComponents;
	colorBlend.blendEnable = blendEnabled;
	colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	colorBlend.dstColorBlendFactor = blendFactor;
	colorBlend.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlend.alphaBlendOp = VK_BLEND_OP_ADD;
}
void PipelineConfigs::setColorAttachmentAndDepthFormat(VkFormat& colorAttachmentFormat, VkFormat colorFormat, VkPipelineRenderingCreateInfo& renderInfo, VkFormat depthFormat) {
	colorAttachmentFormat = colorFormat;

	if (colorFormat != VK_FORMAT_UNDEFINED) {
		renderInfo.colorAttachmentCount = 1;
		renderInfo.pColorAttachmentFormats = &colorAttachmentFormat;
	}
	else {
		renderInfo.colorAttachmentCount = 0;
		renderInfo.pColorAttachmentFormats = nullptr;
	}

	renderInfo.depthAttachmentFormat = (depthFormat != VK_FORMAT_UNDEFINED) ? depthFormat : VK_FORMAT_UNDEFINED;
}
void PipelineConfigs::depthStencilConfig(VkPipelineDepthStencilStateCreateInfo& depthStencil, bool depthTestEnabled, bool depthWriteEnabled, bool depthBoundsTestEnabled, bool stencilTestEnabled, VkCompareOp depthCompare) {
	depthStencil.depthTestEnable = depthTestEnabled;
	depthStencil.depthWriteEnable = depthWriteEnabled;
	depthStencil.depthCompareOp = depthCompare;
	depthStencil.depthBoundsTestEnable = depthBoundsTestEnabled;
	depthStencil.stencilTestEnable = stencilTestEnabled;
	depthStencil.front = {};
	depthStencil.back = {};
	depthStencil.minDepthBounds = 0.f;
	depthStencil.maxDepthBounds = 1.f;
}