#include "pch.h"

#include "PipelineManager.h"

#include "vulkan/Backend.h"
#include "renderer/Renderer.h"
#include "renderer/RenderScene.h"

// TODO: add push constants as parameter
VkPipelineLayout PipelineManager::setupPipelineLayout(PipelineConfigPresent& pipelineInfo) {
	PushConstantDef matrixRange = {
		.enabled = pipelineInfo.pushConstantsInfo.enabled,
		.offset = pipelineInfo.pushConstantsInfo.offset,
		.size = pipelineInfo.pushConstantsInfo.size,
		.stageFlags = pipelineInfo.pushConstantsInfo.stageFlags
	};

	VkPipelineLayout pipelineLayout;
	createPipelineLayout(pipelineLayout, pipelineInfo.descriptorSetInfo, &matrixRange);

	return pipelineLayout;
}

// THE PIPELINE MANAGER
VkPipelineShaderStageCreateInfo PipelineManager::createPipelineShaderStage(VkShaderStageFlagBits stage, VkShaderModule shaderModule) {
	VkPipelineShaderStageCreateInfo shaderStageInfo{};
	shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStageInfo.stage = stage;
	shaderStageInfo.module = shaderModule;
	shaderStageInfo.pName = "main";

	return shaderStageInfo;
}

void PipelineManager::createPipelineLayout(VkPipelineLayout& pipelineLayout, DescriptorsCentral& descriptors, const PushConstantDef* pushConstants) {
	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.flags = 0;

	if (!descriptors.enableDescriptorsSetAndLayout) {
		pipelineLayoutInfo.setLayoutCount = 0;
		pipelineLayoutInfo.pSetLayouts = nullptr;
	}
	else {
		pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptors.descriptorLayouts.size());
		pipelineLayoutInfo.pSetLayouts = descriptors.descriptorLayouts.data();
	}

	VkPushConstantRange pushConstantRange{};
	if (pushConstants && pushConstants->enabled) {
		pushConstantRange.stageFlags = pushConstants->stageFlags;
		pushConstantRange.offset     = pushConstants->offset;
		pushConstantRange.size       = pushConstants->size;
		pipelineLayoutInfo.pushConstantRangeCount = 1;
		pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
	}
	else {
		pipelineLayoutInfo.pushConstantRangeCount = 0;
		pipelineLayoutInfo.pPushConstantRanges = nullptr;
	}

	VK_CHECK(vkCreatePipelineLayout(Backend::getDevice(), &pipelineLayoutInfo, nullptr, &pipelineLayout));
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

void PipelineManager::initPipelines() {
	// compute pipeline
	// using older method, push constants stored in pipeline struct

	// compute pipelines push constant
	Pipelines::drawImagePipeline._pushConstantInfo = {
		.enabled = true,
		.offset = 0,
		.size = static_cast<uint32_t>(sizeof(PushConstantBlock)),
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
	};

	PipelineConfigPresent computePipelinePCInfo;
	computePipelinePCInfo.pushConstantsInfo = Pipelines::drawImagePipeline._pushConstantInfo;

	// descriptor sets def
	computePipelinePCInfo.descriptorSetInfo.descriptorLayouts = DescriptorSetOverwatch::getDrawImageDescriptors().descriptorLayouts;

	Pipelines::drawImagePipeline.getComputePipelineLayout() = setupPipelineLayout(computePipelinePCInfo);

	Pipelines::drawImagePipeline.createComputePipeline(Engine::getDeletionQueue());


	// Default pipelines setup
	Pipelines::metalRoughMatConfigs.pushConstantsInfo = {
		.enabled = true,
		.offset = 0,
		.size = static_cast<uint32_t>(sizeof(GPUDrawPushConstants)),
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT
	};

	RenderScene::metalRoughMaterial.buildPipelines(Pipelines::metalRoughMatConfigs);
}

void PipelineManager::setupPipelineConfig(PipelineBuilder& pipeline, PipelineConfigPresent& settings) {

	PipelineConfigs::inputAssemblyConfig(pipeline._inputAssembly, settings.topology, VK_FALSE);

	if (settings.enableBackfaceCulling) {
		PipelineConfigs::rasterizerConfig(pipeline._rasterizer, settings.polygonMode, 1.f, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
	}
	else {
		PipelineConfigs::rasterizerConfig(pipeline._rasterizer, settings.polygonMode, 1.f, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);
	}

	PipelineConfigs::multisamplingConfig(pipeline._multisampling, Renderer::getAvailableSampleCounts(), Renderer::getCurrentSampleCount(), VK_FALSE);

	PipelineConfigs::colorBlendingConfig(pipeline._colorBlendAttachment,
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT, settings.enableBlending, VK_BLEND_FACTOR_ONE);

	if (settings.enableDepthTest) {
		PipelineConfigs::depthStencilConfig(pipeline._depthStencil, VK_TRUE, VK_TRUE, VK_FALSE, VK_FALSE, settings.depthCompareOp);
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
void PipelineConfigs::multisamplingConfig(VkPipelineMultisampleStateCreateInfo& multisampling,
	const std::vector<VkSampleCountFlags>& samples, uint32_t chosenMSAACount, bool sampleShadingEnabled) {

	bool isPowerOfTwo = (chosenMSAACount != 0) && ((chosenMSAACount & (chosenMSAACount - 1)) == 0);
	bool isWithinRange = (chosenMSAACount <= 64);
	if (!isPowerOfTwo || !isWithinRange) {
		throw std::runtime_error("Invalid MSAA count! Must be a power of two up to 64.");
	}

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
		if (!found) {
			throw std::runtime_error("Failed to find valid sample count!");
		}
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
	colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	colorBlend.alphaBlendOp = VK_BLEND_OP_ADD;
}
void PipelineConfigs::setColorAttachmentAndDepthFormat(VkFormat& colorAttachmentformat, VkFormat colorFormat, VkPipelineRenderingCreateInfo& renderInfo, VkFormat depthFormat) {
	colorAttachmentformat = colorFormat;
	renderInfo.colorAttachmentCount = 1;
	renderInfo.pColorAttachmentFormats = &colorAttachmentformat;

	renderInfo.depthAttachmentFormat = depthFormat;
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