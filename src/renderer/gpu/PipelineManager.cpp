#include "pch.h"

#include "PipelineManager.h"
#include "renderer/gpu/Descriptor.h"
#include "renderer/backend/Backend.h"
#include "core/ResourceManager.h"

namespace PipelinePresents {
	inline std::array<PipelinePresent, (size_t)PipelineID::Count> pipelinePresentBuilder;
	static inline PipelinePresent& getPipelinePresentByID(PipelineID id) {
		return pipelinePresentBuilder[static_cast<size_t>(id)];
	}
}

void PipelineManager::initShaders(DeletionQueue& dq) {
	// === GRAPHIC PIPELINES ===

	std::vector<ShaderStageInfo> meshShaderStages;
	ShaderStageInfo vertexStage {
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.filePath = "res/shaders/meshes/mesh_vert.spv"
	};
	ShaderStageInfo fragmentStage {
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		.filePath = "res/shaders/meshes/mesh_frag.spv"
	};

	meshShaderStages.push_back(vertexStage);
	meshShaderStages.push_back(fragmentStage);

	PipelinePresents::getPipelinePresentByID(PipelineID::Opaque).shaderStagesInfo = meshShaderStages;
	PipelinePresents::getPipelinePresentByID(PipelineID::Transparent).shaderStagesInfo = meshShaderStages;
	PipelinePresents::getPipelinePresentByID(PipelineID::Wireframe).shaderStagesInfo = meshShaderStages;

	// separate shaders needed for bounding boxes
	ShaderStageInfo bbVertStage {
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.filePath = "res/shaders/debug/aabb_vert.spv"
	};
	ShaderStageInfo bbFragStage {
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		.filePath = "res/shaders/debug/aabb_frag.spv"
	};
	PipelinePresents::getPipelinePresentByID(PipelineID::BoundingBox).shaderStagesInfo.push_back(bbVertStage);
	PipelinePresents::getPipelinePresentByID(PipelineID::BoundingBox).shaderStagesInfo.push_back(bbFragStage);


	ShaderStageInfo skyboxVertStage {
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.filePath = "res/shaders/environment/skybox_vert.spv"
	};
	ShaderStageInfo skyboxFragStage {
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		.filePath = "res/shaders/environment/skybox_frag.spv"
	};
	PipelinePresents::getPipelinePresentByID(PipelineID::Skybox).shaderStagesInfo.push_back(skyboxVertStage);
	PipelinePresents::getPipelinePresentByID(PipelineID::Skybox).shaderStagesInfo.push_back(skyboxFragStage);

	// === COMPUTE PIPELINES ===

	// TONE MAP
	ShaderStageInfo toneMapShaderStage {
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.filePath = "res/shaders/post_process/tone_map_comp.spv"
	};
	PipelinePresents::getPipelinePresentByID(PipelineID::ToneMap).shaderStagesInfo.push_back(toneMapShaderStage);


	// ENVIRONMENTAL AND IBL
	ShaderStageInfo cubemapShaderStage {
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.filePath = "res/shaders/environment/hdr2cubemap_comp.spv"
	};
	PipelinePresents::getPipelinePresentByID(PipelineID::HDRToCubemap).shaderStagesInfo.push_back(cubemapShaderStage);

	ShaderStageInfo prefilterShaderStage {
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.filePath = "res/shaders/environment/specular_prefilter_comp.spv"
	};

	PipelinePresents::getPipelinePresentByID(PipelineID::SpecularPrefilter).shaderStagesInfo.push_back(prefilterShaderStage);

	ShaderStageInfo diffuseShaderStage {
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.filePath = "res/shaders/environment/diffuse_irradiance_comp.spv"
	};
	PipelinePresents::getPipelinePresentByID(PipelineID::DiffuseIrradiance).shaderStagesInfo.push_back(diffuseShaderStage);

	ShaderStageInfo brdfLutShaderStage {
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.filePath = "res/shaders/environment/brdf_lut_comp.spv"
	};
	PipelinePresents::getPipelinePresentByID(PipelineID::BRDFLUT).shaderStagesInfo.push_back(brdfLutShaderStage);


	// gpu frustum culling
	ShaderStageInfo visibilityShaderStage {
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.filePath = "res/shaders/visibility/visibility_comp.spv"
	};
	PipelinePresents::getPipelinePresentByID(PipelineID::Visibility).shaderStagesInfo.push_back(visibilityShaderStage);


	// Pipeline shaders defined, good to setup
	for (size_t i = 0; i < static_cast<size_t>(PipelineID::Count); ++i) {
		setupShaders(PipelinePresents::pipelinePresentBuilder[i], dq);
	}
}

// defines push constants, descriptors, and pipeline layout
void PipelineManager::definePipelineData() {

	// === PIPELINE DEFAULT ===
	uint32_t maxPCsize = Backend::getDeviceLimits().maxPushConstantsSize;

	if (maxPCsize >= MAX_PUSH_CONSTANT_SIZE) {
		fmt::print("Device max push constant size: {}\nEngine limit is 256 bytes.\n", maxPCsize);
		maxPCsize = MAX_PUSH_CONSTANT_SIZE;
	}
	else {
		assert(false && "GPU doesn't support required 256 byte push constant size!");
	}

	const PushConstantDef pcRange {
		.offset = 0,
		.size = maxPCsize,
		.stageFlags = VK_SHADER_STAGE_ALL
	};

	const std::vector<VkDescriptorSetLayout> setLayouts {
		DescriptorSetOverwatch::getUnifiedDescriptors().descriptorLayout, // set: 0
		DescriptorSetOverwatch::getFrameDescriptors().descriptorLayout    // set: 1
	};

	Pipelines::_globalLayout.layout = PipelineManager::createPipelineLayout(setLayouts, pcRange);
	Pipelines::_globalLayout.pcRange = pcRange;
}

void PipelineManager::initPipelines(DeletionQueue& queue) {
	DeletionQueue shaderDeletionQ;

	initShaders(shaderDeletionQ);

	definePipelineData();

	const auto device = Backend::getDevice();

	// graphic pipelines can share the same builder
	PipelineBuilder builder;
	builder._pipelineLayout = Pipelines::_globalLayout.layout;

	builder.colorFormat = ResourceManager::getDrawImage().imageFormat;
	builder.depthFormat = ResourceManager::getDepthImage().imageFormat;

	auto createPipeline = [&](PipelineID id, PipelineCategory type, std::string name, bool swappable = false) {
		PipelinePresent& present = PipelinePresents::getPipelinePresentByID(id);
		if (type == PipelineCategory::Raster) {
			builder.initializePipelineSTypes();
			// can overwrite the format if wanted
			if (present.colorFormat == VK_FORMAT_UNDEFINED && present.depthFormat == VK_FORMAT_UNDEFINED) {
				present.colorFormat = builder.colorFormat;
				present.depthFormat = builder.depthFormat;
			}
			setupPipelineConfig(builder, present, MSAA_ENABLED);
		}
		PipelineHandle& pipeHdl = Pipelines::getPipelineHandleByID(id);
		pipeHdl.name = name;
		pipeHdl.type = type;
		pipeHdl.swappable = swappable;

		builder.createPipeline(pipeHdl, present, device);
	};

	// === OPAQUE PIPELINE ===
	createPipeline(PipelineID::Opaque, PipelineCategory::Raster, "Opaque", true);


	// === TRANSPARENT PIPELINE ===
	PipelinePresent& transPresent = PipelinePresents::getPipelinePresentByID(PipelineID::Transparent);
	transPresent.enableBlending = true;
	transPresent.enableDepthWrite = false;
	createPipeline(PipelineID::Transparent, PipelineCategory::Raster, "Transparent", true);


	// === WIREFRAME PIPELINE ===
	PipelinePresent& wirePresent = PipelinePresents::getPipelinePresentByID(PipelineID::Wireframe);
	wirePresent.polygonMode = VK_POLYGON_MODE_LINE;
	wirePresent.depthCompareOp = VK_COMPARE_OP_LESS;

	createPipeline(PipelineID::Wireframe, PipelineCategory::Raster, "Wireframe", true);

	// === BOUNDINGBOX PIPELINE ===
	PipelinePresent& bbPresent = PipelinePresents::getPipelinePresentByID(PipelineID::BoundingBox);
	bbPresent.polygonMode = VK_POLYGON_MODE_LINE;
	bbPresent.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	bbPresent.enableDepthWrite = false;
	bbPresent.depthCompareOp = VK_COMPARE_OP_LESS;

	createPipeline(PipelineID::BoundingBox, PipelineCategory::Raster, "BoundingBox");

	// === SKYBOX PIPELINE ===
	PipelinePresent& skyboxPresent = PipelinePresents::getPipelinePresentByID(PipelineID::Skybox);
	skyboxPresent.enableDepthWrite = false;

	createPipeline(PipelineID::Skybox, PipelineCategory::Raster, "Skybox");

	// === COMPUTE PIPELINE SETUP STAGE ===

	createPipeline(PipelineID::Visibility, PipelineCategory::Compute, "Visibility");
	createPipeline(PipelineID::ToneMap, PipelineCategory::Compute, "ToneMap");
	createPipeline(PipelineID::HDRToCubemap, PipelineCategory::Compute, "HDRToCubemap");
	createPipeline(PipelineID::SpecularPrefilter, PipelineCategory::Compute, "SpecularPrefilter");
	createPipeline(PipelineID::DiffuseIrradiance, PipelineCategory::Compute, "DiffuseIrradiance");
	createPipeline(PipelineID::BRDFLUT, PipelineCategory::Compute, "BRDFLUT");

	shaderDeletionQ.flush(); // deferred deletion of shader modules

	for (size_t i = 0; i < static_cast<size_t>(PipelineID::Count); ++i) {
		queue.push_function([=] {
			VkPipeline pipeline = Pipelines::getPipelineByID(static_cast<PipelineID>(i));
			vkDestroyPipeline(device, pipeline, nullptr);
		});
	}

	queue.push_function([=] {
		vkDestroyPipelineLayout(device, Pipelines::_globalLayout.layout, nullptr);
	});
}

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

void PipelineManager::setupShaders(PipelinePresent& pipelineSettings, DeletionQueue& shaderDeletionQueue) {
	pipelineSettings.shaderStages.clear();

	for (auto& shaders : pipelineSettings.shaderStagesInfo) {
		VkPipelineShaderStageCreateInfo shader = setShader(shaders.filePath, shaders.stage, shaderDeletionQueue);
		pipelineSettings.shaderStages.push_back(shader);
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

void PipelineManager::setupPipelineConfig(PipelineBuilder& pipeline, PipelinePresent& settings, bool msaaOn) {

	PipelineConfigs::inputAssemblyConfig(pipeline._inputAssembly, settings.topology, VK_FALSE);

	PipelineConfigs::rasterizerConfig(pipeline._rasterizer, settings.polygonMode, 1.0f, settings.cullMode, settings.frontFace);

	if (msaaOn) {
		PipelineConfigs::multisamplingConfig(pipeline._multisampling,
			ResourceManager::getAvailableSampleCounts(), CURRENT_MSAA_LVL, VK_FALSE);
	}
	else {
		PipelineConfigs::multisamplingConfig(pipeline._multisampling,
			ResourceManager::getAvailableSampleCounts(), 1, VK_FALSE);
	}

	PipelineConfigs::colorBlendingConfig(pipeline._colorBlendAttachment,
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
		settings.enableBlending, VK_BLEND_FACTOR_ONE);

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

void PipelineConfigs::inputAssemblyConfig(
	VkPipelineInputAssemblyStateCreateInfo& inputAssembly,
	VkPrimitiveTopology topology,
	bool primitiveRestartEnabled)
{
	inputAssembly.topology = topology;
	inputAssembly.primitiveRestartEnable = primitiveRestartEnabled;
}
void PipelineConfigs::rasterizerConfig(
	VkPipelineRasterizationStateCreateInfo& rasterizer,
	VkPolygonMode mode,
	float lineWidth,
	VkCullModeFlags cullMode,
	VkFrontFace frontFace)
{
	rasterizer.polygonMode = mode;
	rasterizer.lineWidth = lineWidth;
	rasterizer.cullMode = cullMode;
	rasterizer.frontFace = frontFace;
}

void PipelineConfigs::multisamplingConfig(
	VkPipelineMultisampleStateCreateInfo& multisampling,
	const std::vector<VkSampleCountFlags>& samples,
	uint32_t chosenMSAACount,
	bool sampleShadingEnabled)
{

	ASSERT((chosenMSAACount != 0) && ((chosenMSAACount & (chosenMSAACount - 1)) == 0) && "Invalid MSAA count! Must be a power of two up to 8.");
	ASSERT(chosenMSAACount <= 8 && "Invalid MSAA count! Must be a power of two up to 8.");

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
		ASSERT(found && "Failed to find valid MSAA sample count!");
	}

	multisampling.sampleShadingEnable = sampleShadingEnabled;
	multisampling.minSampleShading = 1.0f;
	multisampling.pSampleMask = nullptr;

	multisampling.alphaToCoverageEnable = VK_TRUE;
	multisampling.alphaToOneEnable = VK_FALSE;
}

void PipelineConfigs::colorBlendingConfig(
	VkPipelineColorBlendAttachmentState& colorBlend,
	VkColorComponentFlags colorComponents,
	bool blendEnabled,
	VkBlendFactor blendFactor)
{
	colorBlend.colorWriteMask = colorComponents;
	colorBlend.blendEnable = blendEnabled;
	colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	colorBlend.dstColorBlendFactor = blendFactor;
	colorBlend.colorBlendOp = VK_BLEND_OP_ADD;
	colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlend.alphaBlendOp = VK_BLEND_OP_ADD;
}
void PipelineConfigs::setColorAttachmentAndDepthFormat(
	VkFormat& colorAttachmentFormat,
	VkFormat colorFormat,
	VkPipelineRenderingCreateInfo& renderInfo,
	VkFormat depthFormat)
{
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
void PipelineConfigs::depthStencilConfig(
	VkPipelineDepthStencilStateCreateInfo& depthStencil,
	bool depthTestEnabled,
	bool depthWriteEnabled,
	bool depthBoundsTestEnabled,
	bool stencilTestEnabled,
	VkCompareOp depthCompare)
{
	depthStencil.depthTestEnable = depthTestEnabled;
	depthStencil.depthWriteEnable = depthWriteEnabled;
	depthStencil.depthCompareOp = depthCompare;
	depthStencil.depthBoundsTestEnable = depthBoundsTestEnabled;
	depthStencil.stencilTestEnable = stencilTestEnabled;
	depthStencil.front = {};
	depthStencil.back = {};
	depthStencil.minDepthBounds = 0.0f;
	depthStencil.maxDepthBounds = 1.0f;
}