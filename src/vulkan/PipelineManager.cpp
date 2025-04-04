#include "pch.h"

#include "PipelineManager.h"

#include "vulkan/Backend.h"
#include "renderer/Renderer.h"
#include "renderer/RenderScene.h"

// TODO: need better way to setup scalability for push constant use
void PipelineManager::setupPipelineLayouts() {
	VkPhysicalDevice physicalDevice = Backend::getPhysicalDevice();

	// Set the push constant pool for the pipeline layouts that need it
	PushConstantPool pcPool = VulkanUtils::CreatePushConstantPool(physicalDevice);

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


	PushConstantDef meshPC = {
		.enabled = true,
		.offset = 0,
		.size = static_cast<uint32_t>(sizeof(GPUDrawPushConstants)), // 72 bytes
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT
	};

	DescriptorsCentral meshDescriptors = {
		.descriptorLayout = RenderScene::getGPUSceneDescriptorLayout()
	};

	Pipelines::meshPipeline._pushConstantInfo = meshPC;
	PipelineManager::createPipelineLayout(Pipelines::meshPipeline._pipelineLayout, meshDescriptors, &meshPC);
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
		.filePath = "res/shaders/tex_image_frag.spv",
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT
	};

	//ShaderStageInfo color = {
	//	.filePath = "res/shaders/colored_frag.spv",
	//	.stage = VK_SHADER_STAGE_FRAGMENT_BIT
	//};

	triangleMeshShaderStages.push_back(meshTriangle);
	triangleMeshShaderStages.push_back(color);
	setupShaders(Pipelines::meshPipeline, triangleMeshShaderStages, shaderDeletionQ);

	setupGraphicsPipelineCofig(Pipelines::meshPipeline);

	Pipelines::meshPipeline.createPipeline();

	shaderDeletionQ.flush();

	Engine::getDeletionQueue().push_function([=]() {
		vkDestroyPipelineLayout(Backend::getDevice(), Pipelines::meshPipeline.getPipelineLayout(), nullptr);
		Pipelines::meshPipeline._pipelineLayout = VK_NULL_HANDLE;
		vkDestroyPipeline(Backend::getDevice(), Pipelines::meshPipeline.getPipeline(), nullptr);
		Pipelines::meshPipeline._pipeline = VK_NULL_HANDLE;
	});
}

void PipelineManager::setupGraphicsPipelineCofig(GraphicsPipeline& pipeline) {
	// Build the standard graphics pipeline stages
	pipeline.initializePipelineSTypes(); // might delete this

	PipelineConfigs::inputAssemblyConfig(pipeline._inputAssembly, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE);

	PipelineConfigs::rasterizerConfig(pipeline._rasterizer, VK_POLYGON_MODE_FILL, 1.f, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);

	PipelineConfigs::multisamplingConfig(pipeline._multisampling, Renderer::getAvailableSampleCounts(), Renderer::getCurrentSampleCount(), VK_FALSE);

	PipelineConfigs::colorBlendingConfig(pipeline._colorBlendAttachment,
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT, VK_TRUE, VK_BLEND_FACTOR_ONE);

	PipelineConfigs::depthStencilConfig(pipeline._depthStencil, VK_TRUE, VK_TRUE, VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);

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