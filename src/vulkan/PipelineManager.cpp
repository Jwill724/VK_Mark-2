#include "pch.h"

#include "PipelineManager.h"

#include "vulkan/Backend.h"
#include "renderer/renderer.h"

// TODO: need better way to setup scalability for push constant use
void PipelineManager::setupPipelineLayouts() {
	VkPhysicalDevice physicalDevice = Backend::getPhysicalDevice();

	// Set the push constant pool for the pipeline layouts that need it
	PushConstantPool pcPool = VulkanUtils::CreatePushConstantPool(physicalDevice);
//	uint32_t pcOffset = 0;

	// Compute pipeline draw image
//	setupPushConstantBlock(pcPool, pcOffset);

	//bool pcAlloc = VulkanUtils::AllocatePushConstant(&pcPool, static_cast<uint32_t>(sizeof(PushConstantBlock)), &pcOffset);
	//if (!pcAlloc) {
	//	throw std::runtime_error("Failed to allocate push constants!");
	//}

	PushConstantDef drawImagePC = {
		.enabled = true,
		.offset = 0,
		.size = static_cast<uint32_t>(sizeof(PushConstantBlock)), 	// 64 bytes in size per block
		.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
	};

	// pipeline instances hold push constant information to share among shaders or textures
	Pipelines::drawImagePipeline._pushConstantInfo = drawImagePC;

	PipelineManager::createPipelineLayout(Pipelines::drawImagePipeline._computePipelineLayout, DescriptorSetOverwatch::getDrawImageDescriptors(),
		&drawImagePC);

	//// vertex shader mesh
	//pcAlloc = VulkanUtils::AllocatePushConstant(&pcPool, static_cast<uint32_t>(sizeof(GPUDrawPushConstants)), &pcOffset);
	//if (!pcAlloc) {
	//	throw std::runtime_error("Failed to allocate push constants!");
	//}

	PushConstantDef meshPC = {
		.enabled = true,
		.offset = 0,
		.size = static_cast<uint32_t>(sizeof(GPUDrawPushConstants)), // 72 bytes
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT
	};

	Pipelines::meshPipeline._pushConstantInfo = meshPC;

	PipelineManager::createPipelineLayout(Pipelines::meshPipeline._pipelineLayout, DescriptorSetOverwatch::getMeshesDescriptors(),
		&meshPC);
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

VkSampleCountFlagBits PipelineManager::getMaxUsableSampleCount() {
	VkPhysicalDeviceProperties physicalDeviceProperties;
	vkGetPhysicalDeviceProperties(Backend::getPhysicalDevice(), &physicalDeviceProperties);

	VkSampleCountFlags counts =
		physicalDeviceProperties.limits.framebufferColorSampleCounts &
		physicalDeviceProperties.limits.framebufferDepthSampleCounts;
	if (counts & VK_SAMPLE_COUNT_64_BIT) { return VK_SAMPLE_COUNT_64_BIT; }
	if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
	if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
	if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
	if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
	if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }

	return VK_SAMPLE_COUNT_1_BIT;
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
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &descriptors.descriptorLayout;
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

void PipelineManager::setupShaders(GraphicsPipeline& pipeline, std::vector<ShaderStageInfo>& shaderStageInfo, DeletionQueue& shaderDeletionQueue) {
	pipeline._shaderStages.clear();

	for (auto& shaders : shaderStageInfo) {
		VkPipelineShaderStageCreateInfo shader = pipeline.setShader(shaders.filePath, shaders.stage, shaderDeletionQueue);
		pipeline._shaderStages.push_back(shader);
	}
}

void PipelineManager::initPipelines() {
	// setup shader module deletion and flush at the end
	DeletionQueue shaderDeletionQ;

	setupPipelineLayouts();

	// COMPUTE PIPELINE
	Pipelines::drawImagePipeline.createComputePipeline(Engine::getDeletionQueue());

	// eventually asset loading will need this
	std::vector<ShaderStageInfo> triangleMeshShaderStages;
	ShaderStageInfo meshTriangle = {
		.filePath = "res/shaders/triangle_mesh_vert.spv",
		.stage = VK_SHADER_STAGE_VERTEX_BIT
	};

	ShaderStageInfo color = {
		.filePath = "res/shaders/colored_frag.spv",
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT
	};

	triangleMeshShaderStages.push_back(meshTriangle);
	triangleMeshShaderStages.push_back(color);
	setupShaders(Pipelines::meshPipeline, triangleMeshShaderStages, shaderDeletionQ);

	setupGraphicsPipelineCofig(Pipelines::meshPipeline);

	Pipelines::meshPipeline.createPipeline();

	shaderDeletionQ.flush();

	Engine::getDeletionQueue().push_function([=]() {
		vkDestroyPipelineLayout(Backend::getDevice(), Pipelines::meshPipeline.getPipelineLayout(), nullptr);
		vkDestroyPipeline(Backend::getDevice(), Pipelines::meshPipeline.getPipeline(), nullptr);
	});
}

// TODO: modularize pipeline config further
//struct GraphicsPipelineOverrides {
//	VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
//	VkCullModeFlags cullMode = VK_CULL_MODE_NONE;
//	VkFrontFace frontFace = VK_FRONT_FACE_CLOCKWISE;
//	VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
//	bool enableDepth = true;
//	bool enableBlending = false;
//	// Add more overrides as needed
//};

void PipelineManager::setupGraphicsPipelineCofig(GraphicsPipeline& pipeline) {
	// Build the standard graphics pipeline stages
	pipeline.initializePipelineSTypes(); // might delete this

	PipelineConfigs::inputAssemblyConfig(pipeline._inputAssembly, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE);

	PipelineConfigs::rasterizerConfig(pipeline._rasterizer, VK_POLYGON_MODE_FILL, 1.f, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);

	PipelineConfigs::multisamplingConfig(pipeline._multisampling, VK_SAMPLE_COUNT_1_BIT, VK_FALSE);

	PipelineConfigs::colorBlendingConfig(pipeline._colorBlendAttachment,
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT, VK_FALSE);

	PipelineConfigs::depthStencilConfig(pipeline._depthStencil, true, true, VK_FALSE, VK_FALSE, VK_COMPARE_OP_GREATER_OR_EQUAL);

	PipelineConfigs::setColorAttachmentAndDepthFormat(pipeline._colorAttachmentformat,
		Renderer::getDrawImage().imageFormat, pipeline._renderInfo, Renderer::getDepthImage().imageFormat);
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
// only first two configurable for now
void PipelineConfigs::multisamplingConfig(VkPipelineMultisampleStateCreateInfo& multisampling, VkSampleCountFlagBits msaaSamples, bool sampleShadingEnabled) {
	multisampling.sampleShadingEnable = sampleShadingEnabled;
	multisampling.rasterizationSamples = msaaSamples;
	multisampling.minSampleShading = 1.0f;
	multisampling.pSampleMask = nullptr;
	// no alpha to coverage either
	multisampling.alphaToCoverageEnable = VK_FALSE;
	multisampling.alphaToOneEnable = VK_FALSE;
}
void PipelineConfigs::colorBlendingConfig(VkPipelineColorBlendAttachmentState& colorBlend, VkColorComponentFlags colorComponents, bool blendEnabled) {
	colorBlend.colorWriteMask = colorComponents;
	colorBlend.blendEnable = blendEnabled;
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