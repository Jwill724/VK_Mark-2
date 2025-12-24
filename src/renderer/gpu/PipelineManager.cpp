#include "pch.h"

#include "PipelineManager.h"
#include "renderer/gpu/Descriptor.h"
#include "renderer/backend/Backend.h"
#include "core/ResourceManager.h"

namespace PipelinePresents {
	inline std::array<PipelinePreset, (size_t)PipelineID::Count> pipelinePresentBuilder;
	inline PipelinePreset& getPipelinePresentByID(PipelineID id) {
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


	ShaderStageInfo obbLineVertStage {
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.filePath = "res/shaders/debug/obb_line_vert.spv"
	};
	ShaderStageInfo obbLineFragStage {
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		.filePath = "res/shaders/debug/obb_line_frag.spv"
	};
	PipelinePresents::getPipelinePresentByID(PipelineID::OBBLine).shaderStagesInfo.push_back(obbLineVertStage);
	PipelinePresents::getPipelinePresentByID(PipelineID::OBBLine).shaderStagesInfo.push_back(obbLineFragStage);

	ShaderStageInfo cascadeVPLineVertStage {
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.filePath = "res/shaders/debug/cascadevp_line_vert.spv"
	};
	ShaderStageInfo cascadeVPLineFragStage {
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		.filePath = "res/shaders/debug/cascadevp_line_frag.spv"
	};
	PipelinePresents::getPipelinePresentByID(PipelineID::CascadeVPLine).shaderStagesInfo.push_back(cascadeVPLineVertStage);
	PipelinePresents::getPipelinePresentByID(PipelineID::CascadeVPLine).shaderStagesInfo.push_back(cascadeVPLineFragStage);


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

	// === DEPTH PRE-PASS ===
	ShaderStageInfo depthPrepassVertStage{
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.filePath = "res/shaders/depth/depth_prepass_vert.spv"
	};
	ShaderStageInfo depthPrepassFragStage{
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		.filePath = "res/shaders/depth/depth_prepass_frag.spv"
	};
	PipelinePresents::getPipelinePresentByID(PipelineID::DepthPrepass).shaderStagesInfo.push_back(depthPrepassVertStage);
	PipelinePresents::getPipelinePresentByID(PipelineID::DepthPrepass).shaderStagesInfo.push_back(depthPrepassFragStage);

	// === CASCADED SHADOW MAPPING ===
	ShaderStageInfo csmVertStage{
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.filePath = "res/shaders/depth/csm_depth_vert.spv"
	};
	PipelinePresents::getPipelinePresentByID(PipelineID::ShadowCSM).shaderStagesInfo.push_back(csmVertStage);

	// === COMPUTE PIPELINES ===

	// === EXPOSURE ===
	ShaderStageInfo exposureReduceShaderStage {
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.filePath = "res/shaders/post_process/exposure_reduce_comp.spv"
	};
	PipelinePresents::getPipelinePresentByID(PipelineID::ExposureReduce).shaderStagesInfo.push_back(exposureReduceShaderStage);

	ShaderStageInfo exposureFinalizeShaderStage {
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.filePath = "res/shaders/post_process/exposure_finalize_comp.spv"
	};
	PipelinePresents::getPipelinePresentByID(PipelineID::ExposureFinalize).shaderStagesInfo.push_back(exposureFinalizeShaderStage);

	// === TONE MAP ===
	ShaderStageInfo toneMapShaderStage {
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.filePath = "res/shaders/post_process/tone_map_comp.spv"
	};
	PipelinePresents::getPipelinePresentByID(PipelineID::ToneMap).shaderStagesInfo.push_back(toneMapShaderStage);


	// === DEPTH PYRAMID ===
	ShaderStageInfo depthPyramidShaderStage {
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.filePath = "res/shaders/depth/depth_pyramid_comp.spv"
	};
	PipelinePresents::getPipelinePresentByID(PipelineID::DepthPyramid).shaderStagesInfo.push_back(depthPyramidShaderStage);

	// === IBL ===
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


	//// gpu frustum culling
	//ShaderStageInfo visibilityShaderStage {
	//	.stage = VK_SHADER_STAGE_COMPUTE_BIT,
	//	.filePath = "res/shaders/visibility/visibility_comp.spv"
	//};
	//PipelinePresents::getPipelinePresentByID(PipelineID::Visibility).shaderStagesInfo.push_back(visibilityShaderStage);

	// === SSAO ===
	ShaderStageInfo ssaoStage {
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.filePath = "res/shaders/ao/ssao_main_comp.spv"
	};
	PipelinePresents::getPipelinePresentByID(PipelineID::SSAO).shaderStagesInfo.push_back(ssaoStage);

	ShaderStageInfo ssaoBlurStage {
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.filePath = "res/shaders/ao/ssao_blur_comp.spv"
	};
	PipelinePresents::getPipelinePresentByID(PipelineID::SSAOBlur).shaderStagesInfo.push_back(ssaoBlurStage);


	// === GTAO ===
	ShaderStageInfo gtaoStage {
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.filePath = "res/shaders/ao/gtao_main_comp.spv"
	};
	PipelinePresents::getPipelinePresentByID(PipelineID::GTAO).shaderStagesInfo.push_back(gtaoStage);

	ShaderStageInfo gtaoFilterStage {
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.filePath = "res/shaders/ao/gtao_filter_comp.spv"
	};
	PipelinePresents::getPipelinePresentByID(PipelineID::GTAOFilter).shaderStagesInfo.push_back(gtaoFilterStage);

	ShaderStageInfo gtaoTempResolveStage{
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.filePath = "res/shaders/ao/gtao_temporal_resolve_comp.spv"
	};
	PipelinePresents::getPipelinePresentByID(PipelineID::GTAOTemporalResolve).shaderStagesInfo.push_back(gtaoTempResolveStage);


	// === VOLUMETRIC LIGHTING ===
	ShaderStageInfo volLightStage {
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.filePath = "res/shaders/post_process/volumetric_light_comp.spv"
	};
	PipelinePresents::getPipelinePresentByID(PipelineID::VolumetricLight).shaderStagesInfo.push_back(volLightStage);

	ShaderStageInfo volLightBlurStage {
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.filePath = "res/shaders/post_process/volumetric_light_blur_comp.spv"
	};
	PipelinePresents::getPipelinePresentByID(PipelineID::VolumetricLightBlur).shaderStagesInfo.push_back(volLightBlurStage);

	// === LENS FLARE ===
	ShaderStageInfo flareBrightStage {
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.filePath = "res/shaders/post_process/flare_bright_comp.spv"
	};
	PipelinePresents::getPipelinePresentByID(PipelineID::FlareBright).shaderStagesInfo.push_back(flareBrightStage);

	ShaderStageInfo flareGenStage {
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.filePath = "res/shaders/post_process/flare_gen_comp.spv"
	};
	PipelinePresents::getPipelinePresentByID(PipelineID::FlareGen).shaderStagesInfo.push_back(flareGenStage);


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
		maxPCsize = 128u;
		fmt::print("Device fallback to push constant min: {} bytes\n", maxPCsize);
	}

	VkShaderStageFlags pcShaderStages = VK_SHADER_STAGE_VERTEX_BIT |
		VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

	const PushConstantDef pcRange {
		.offset = 0,
		.size = maxPCsize,
		.stageFlags = pcShaderStages
	};

	const std::vector<VkDescriptorSetLayout> setLayouts {
		DescriptorSetOverwatch::getUnifiedDescriptor().descriptorLayout, // set: 0
		DescriptorSetOverwatch::getFrameDescriptor().descriptorLayout,   // set: 1
		DescriptorSetOverwatch::getPushDescriptor().descriptorLayout     // set: 2
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

	builder.colorFormats.push_back(VK_FORMAT_B10G11R11_UFLOAT_PACK32);
	builder.depthFormat = VK_FORMAT_D32_SFLOAT;

	auto createPipeline = [&](
		PipelineID id,
		PipelineCategory type,
		std::string name,
		bool swappable = false,
		bool mssaOn = MSAA_ENABLED) {

		PipelinePreset& present = PipelinePresents::getPipelinePresentByID(id);

		PipelineHandle& pipeHdl = Pipelines::getHandle(id);

		if (type == PipelineCategory::Raster) {
			builder.initializePipelineSTypes();
			// Defaults to primary builder formats,
			// can overwrite the format if wanted
			if (present.colorFormats.empty() && present.depthFormat == VK_FORMAT_UNDEFINED) {
				present.colorFormats = builder.colorFormats;
				present.depthFormat = builder.depthFormat;
			}
			setupPipelineConfig(builder, present, mssaOn);

			pipeHdl.bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

			// Only raster pipelines will get topology info
			pipeHdl.topology = present.topology;
		}
		else {
			pipeHdl.bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
		}

		pipeHdl.name = name;
		pipeHdl.type = type;
		pipeHdl.swappable = swappable;

		builder.createPipeline(pipeHdl, present, device);
	};

	// === OPAQUE PIPELINE ===
	createPipeline(PipelineID::Opaque, PipelineCategory::Raster, "Opaque", false);

	// === TRANSPARENT PIPELINE ===
	PipelinePreset& transparentPreset = PipelinePresents::getPipelinePresentByID(PipelineID::Transparent);
	transparentPreset.colorFormats.push_back(VK_FORMAT_R16G16B16A16_SFLOAT);
	transparentPreset.depthFormat = VK_FORMAT_D32_SFLOAT;
	transparentPreset.enableBlending = true;
	transparentPreset.enableDepthWrite = false;
	createPipeline(PipelineID::Transparent, PipelineCategory::Raster, "Transparent", false, false);

	// === WIREFRAME PIPELINE ===
	PipelinePreset& wirePreset = PipelinePresents::getPipelinePresentByID(PipelineID::Wireframe);
	wirePreset.polygonMode = VK_POLYGON_MODE_LINE;
	wirePreset.depthCompareOp = VK_COMPARE_OP_GREATER;

	createPipeline(PipelineID::Wireframe, PipelineCategory::Raster, "Wireframe", true);

	// === OBB LINE DEBUG PIPELINE ===
	PipelinePreset& lineDebugPreset = PipelinePresents::getPipelinePresentByID(PipelineID::OBBLine);
	lineDebugPreset.polygonMode = VK_POLYGON_MODE_LINE;
	lineDebugPreset.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	lineDebugPreset.enableDepthWrite = false;
	lineDebugPreset.depthCompareOp = VK_COMPARE_OP_GREATER;

	createPipeline(PipelineID::OBBLine, PipelineCategory::Raster, "OBBLine");

	// === CASCADE VIEW PROJECTION LINE DEBUG PIPELINE ===
	PipelinePreset& cascadeVPLinePreset = PipelinePresents::getPipelinePresentByID(PipelineID::CascadeVPLine);
	cascadeVPLinePreset.polygonMode = VK_POLYGON_MODE_LINE;
	cascadeVPLinePreset.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	cascadeVPLinePreset.enableDepthWrite = false;
	cascadeVPLinePreset.depthCompareOp = VK_COMPARE_OP_GREATER;

	createPipeline(PipelineID::CascadeVPLine, PipelineCategory::Raster, "CascadeVPLine");

	// === SKYBOX PIPELINE ===
	PipelinePreset& skyboxPreset = PipelinePresents::getPipelinePresentByID(PipelineID::Skybox);
	skyboxPreset.enableDepthWrite = false;

	createPipeline(PipelineID::Skybox, PipelineCategory::Raster, "Skybox");

	// === DEPTH RESOLVED PIPELINE ===
	PipelinePreset& depthPrePreset = PipelinePresents::getPipelinePresentByID(PipelineID::DepthPrepass);
	depthPrePreset.colorFormats.push_back(VK_FORMAT_A2B10G10R10_UNORM_PACK32); // Normals
	depthPrePreset.colorFormats.push_back(VK_FORMAT_R16G16_SFLOAT);            // Velocity
	depthPrePreset.depthFormat = VK_FORMAT_D32_SFLOAT;
	depthPrePreset.depthCompareOp = VK_COMPARE_OP_GREATER;
	depthPrePreset.cullMode = VK_CULL_MODE_BACK_BIT;

	createPipeline(PipelineID::DepthPrepass, PipelineCategory::Raster, "DepthPrepass", false, false);

	// === CSM PIPELINE ===
	PipelinePreset& csmPreset = PipelinePresents::getPipelinePresentByID(PipelineID::ShadowCSM);
	csmPreset.depthFormat = VK_FORMAT_D32_SFLOAT;
	csmPreset.depthCompareOp = VK_COMPARE_OP_LESS;
	csmPreset.cullMode = VK_CULL_MODE_FRONT_BIT;
	//csmPreset.enableDepthBias = true;
	//csmPreset.depthBiasConstant = 1.25f;
	//csmPreset.depthBiasSlope = 1.75f;
	//csmPreset.depthBiasClamp = 0.0f;
	csmPreset.viewMask = (1u << MAX_SHADOW_CASCADES) - 1u;

	createPipeline(PipelineID::ShadowCSM, PipelineCategory::Raster, "ShadowCSM", false, false);

	// === COMPUTE PIPELINE SETUP STAGE ===
	//createPipeline(PipelineID::Visibility, PipelineCategory::Compute, "Visibility");
	createPipeline(PipelineID::ToneMap, PipelineCategory::Compute, "ToneMap");
	createPipeline(PipelineID::ExposureReduce, PipelineCategory::Compute, "ExposureReduce");
	createPipeline(PipelineID::ExposureFinalize, PipelineCategory::Compute, "ExposureFinalize");
	createPipeline(PipelineID::FlareBright, PipelineCategory::Compute, "FlareBright");
	createPipeline(PipelineID::FlareGen, PipelineCategory::Compute, "FlareGen");
	createPipeline(PipelineID::HDRToCubemap, PipelineCategory::Compute, "HDRToCubemap");
	createPipeline(PipelineID::SpecularPrefilter, PipelineCategory::Compute, "SpecularPrefilter");
	createPipeline(PipelineID::DiffuseIrradiance, PipelineCategory::Compute, "DiffuseIrradiance");
	createPipeline(PipelineID::BRDFLUT, PipelineCategory::Compute, "BRDFLUT");
	createPipeline(PipelineID::SSAO, PipelineCategory::Compute, "SSAO");
	createPipeline(PipelineID::SSAOBlur, PipelineCategory::Compute, "SSAOBlur");
	createPipeline(PipelineID::GTAO, PipelineCategory::Compute, "GTAO");
	createPipeline(PipelineID::GTAOFilter, PipelineCategory::Compute, "GTAOFilter");
	createPipeline(PipelineID::GTAOTemporalResolve, PipelineCategory::Compute, "GTAOTemporalResolve");
	createPipeline(PipelineID::DepthPyramid, PipelineCategory::Compute, "DepthPyramid");
	createPipeline(PipelineID::VolumetricLight, PipelineCategory::Compute, "VolumetricLight");
	createPipeline(PipelineID::VolumetricLightBlur, PipelineCategory::Compute, "VolumetricLightBlur");

	shaderDeletionQ.flush(); // deferred deletion of shader modules

	for (size_t i = 0; i < static_cast<size_t>(PipelineID::Count); ++i) {
		queue.push_function([=] {
			VkPipeline pipeline = Pipelines::getPipeline(static_cast<PipelineID>(i));
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

void PipelineManager::setupShaders(PipelinePreset& pipelineSettings, DeletionQueue& shaderDeletionQueue) {
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

void PipelineManager::setupPipelineConfig(PipelineBuilder& pipeline, PipelinePreset& settings, bool msaaOn) {

	PipelineConfigs::inputAssemblyConfig(pipeline._inputAssembly, settings.topology, VK_FALSE);

	// For cascaded shadow mapping
	if (settings.enableDepthBias) {
		pipeline._rasterizer.depthBiasEnable = VK_TRUE;
		pipeline._rasterizer.depthBiasConstantFactor = settings.depthBiasConstant;
		pipeline._rasterizer.depthBiasSlopeFactor = settings.depthBiasSlope;
		pipeline._rasterizer.depthBiasClamp = settings.depthBiasClamp;
	}
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


	pipeline._renderInfo.viewMask = settings.viewMask;
	PipelineConfigs::setColorAttachmentAndDepthFormat(settings.colorFormats, pipeline._renderInfo, settings.depthFormat);
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
	std::vector<VkFormat>& colorFormats,
	VkPipelineRenderingCreateInfo& renderInfo,
	VkFormat depthFormat)
{
	if (!colorFormats.empty()) {
		renderInfo.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
		renderInfo.pColorAttachmentFormats = colorFormats.data();
	}
	else {
		colorFormats.push_back(VK_FORMAT_UNDEFINED); // No color attachment defined
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