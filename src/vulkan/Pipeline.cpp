#include "pch.h"

#include "Pipeline.h"
#include "PipelineManager.h"
#include "renderer/Descriptor.h"
#include "Backend.h"

VkPipelineShaderStageCreateInfo GraphicsPipeline::setShader(const char* shaderFile, VkShaderStageFlagBits stage, DeletionQueue& shaderDeleteQueue) {
	VkShaderModule shaderModule;
	VulkanUtils::loadShaderModule(shaderFile, Backend::getDevice(), &shaderModule);
	VkPipelineShaderStageCreateInfo shaderStage = PipelineManager::createPipelineShaderStage(stage, shaderModule);

	shaderDeleteQueue.push_function([=] {
		vkDestroyShaderModule(Backend::getDevice(), shaderModule, nullptr);
	});

	return shaderStage;
}

void GraphicsPipeline::initializePipelineSTypes() {
	// clear all of the structs we need back to 0 with their correct stype

	_inputAssembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };

	_rasterizer = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };

	_multisampling = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };

	_depthStencil = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };

	_renderInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
}

void GraphicsPipeline::createPipeline() {
	VkDevice device = Backend::getDevice();

	// make viewport state from our stored viewport and scissor.
	// at the moment we wont support multiple viewports or scissors
	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.pNext = nullptr;

	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	// setup dummy color blending. We arent using transparent objects yet
	// the blending is just "no blend", but we do write to the color attachment
	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.pNext = nullptr;

	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &_colorBlendAttachment;

	VkPipelineVertexInputStateCreateInfo _vertexInputInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

	// build the actual pipeline
    // we now use all of the info structs we have been writing into into this one
    // to create the pipeline
	VkGraphicsPipelineCreateInfo pipelineInfo = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	// connect the renderInfo to the pNext extension mechanism
	pipelineInfo.pNext = &_renderInfo;

	pipelineInfo.stageCount = static_cast<uint32_t>(_shaderStages.size());
	pipelineInfo.pStages = _shaderStages.data();
	pipelineInfo.pVertexInputState = &_vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &_inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &_rasterizer;
	pipelineInfo.pMultisampleState = &_multisampling;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDepthStencilState = &_depthStencil;
	pipelineInfo.layout = _pipelineLayout;

	VkDynamicState state[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

	VkPipelineDynamicStateCreateInfo dynamicInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamicInfo.pDynamicStates = &state[0];
	dynamicInfo.dynamicStateCount = 2;

	pipelineInfo.pDynamicState = &dynamicInfo;

	// its easy to error out on create graphics pipeline, so we handle it a bit
	// better than the common VK_CHECK case
	if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &_pipeline) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create pipeline!");
	}
}

// TODO: modularize the compute pipeline more, need to break the functionality inside into discrete parts.... maybe
void ComputePipeline::createComputePipeline(DeletionQueue& deletionQueue) {
	VkDevice device = Backend::getDevice();

	VkShaderModule computeDrawShader;
	if (!VulkanUtils::loadShaderModule("res/shaders/gradient_comp.spv", device, &computeDrawShader)) {
		throw std::runtime_error("Failed to build compute shader!");
	}

	VkShaderModule skyShader;
	if (!VulkanUtils::loadShaderModule("res/shaders/sky_comp.spv", device, &skyShader)) {
		throw std::runtime_error("Failed to build sky shader!");
	}

	VkPipelineShaderStageCreateInfo stageInfo = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.pNext = nullptr,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = computeDrawShader,
		.pName = "main"
	};

	VkComputePipelineCreateInfo computePipelineCreateInfo = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.pNext = nullptr,
		.stage = stageInfo,
		.layout = _computePipelineLayout,
	};

	PipelineEffect gradient;
	gradient.layout = _computePipelineLayout;
	gradient.name = "gradient";
	gradient.data = {};

	//default colors
	gradient.data.data1 = glm::vec4(1, 0, 0, 1);
	gradient.data.data2 = glm::vec4(0, 0, 1, 1);

	VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &gradient.pipeline));

	computePipelineCreateInfo.stage.module = skyShader;

	PipelineEffect sky;
	sky.layout = _computePipelineLayout;
	sky.name = "sky";
	sky.data = {};

	//default sky parameters
	sky.data.data1 = glm::vec4(0.1, 0.2, 0.4, 0.97);

	VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &sky.pipeline));

	//add the 2 background effects into the array
	backgroundEffects.push_back(gradient);
	backgroundEffects.push_back(sky);

	vkDestroyShaderModule(device, computeDrawShader, nullptr);
	vkDestroyShaderModule(device, skyShader, nullptr);

	deletionQueue.push_function([=] {
		vkDestroyPipelineLayout(Backend::getDevice(), _computePipelineLayout, nullptr);
		vkDestroyPipeline(Backend::getDevice(), sky.pipeline, nullptr);
		vkDestroyPipeline(Backend::getDevice(), gradient.pipeline, nullptr);
	});
}