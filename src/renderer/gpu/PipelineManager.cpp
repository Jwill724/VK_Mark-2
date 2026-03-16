#include "pch.h"

#include "PipelineManager.h"
#include "renderer/gpu/Descriptor.h"
#include "renderer/backend/Backend.h"
#include "core/ResourceManager.h"
#include "engine/Engine.h"

enum VulkanShaderStage {
	COMPUTE_STAGE = VK_SHADER_STAGE_COMPUTE_BIT,
	VERTEX_STAGE = VK_SHADER_STAGE_VERTEX_BIT,
	FRAGMENT_STAGE = VK_SHADER_STAGE_FRAGMENT_BIT
};

namespace PipelineManager {
	void initPipelineShaders(DeletionQueue& dq);
}

namespace PipelinePresets {
	inline std::array<PipelinePreset, static_cast<size_t>(PipelineID::Count)> pipelinePresetBuilder;
	inline PipelinePreset& getPipelinePresetByID(PipelineID id) {
		return pipelinePresetBuilder[static_cast<size_t>(id)];
	}

	std::vector<ShaderStageInfo> shaderStagesInfo;
	std::vector<std::string> shaderStagePathStorage;

	static void writeShaderStage(
		PipelineID id,
		VulkanShaderStage stage,
		const char* shaderName)
	{
		ShaderStageInfo stageInfo{};
		stageInfo.stage = static_cast<VkShaderStageFlagBits>(stage);
		stageInfo.filePath = shaderName;

		getPipelinePresetByID(id).shaderStagesInfo.push_back(stageInfo);
	}

	void createPipeline(
		PipelineBuilder& builder,
		const VkDevice device,
		PipelineID id,
		PipelineCategory type,
		std::string name,
		bool swappable = false);

	PipelineBuilder _defaultBuilder;
	void setupBaseBuilder();
}

void PipelineManager::initPipelineShaders(DeletionQueue& dq) {
	// === GRAPHIC PIPELINES ===
	PipelinePresets::writeShaderStage(PipelineID::Transparent, VERTEX_STAGE, "res/shaders/core/forwardVS.spv");
	PipelinePresets::writeShaderStage(PipelineID::Transparent, FRAGMENT_STAGE, "res/shaders/core/forwardFS.spv");

	PipelinePresets::writeShaderStage(PipelineID::Prepass, VERTEX_STAGE, "res/shaders/core/prepassVS.spv");
	PipelinePresets::writeShaderStage(PipelineID::Prepass, FRAGMENT_STAGE, "res/shaders/core/prepassFS.spv");

	PipelinePresets::writeShaderStage(PipelineID::Shadow, VERTEX_STAGE, "res/shaders/shadows/shadow_depthVS.spv");

	// === COMPUTE PIPELINES ===

	PipelinePresets::writeShaderStage(PipelineID::ExposureReduce, COMPUTE_STAGE, "res/shaders/post_process/exposure_reduce.spv");
	PipelinePresets::writeShaderStage(PipelineID::ExposureFinalize, COMPUTE_STAGE, "res/shaders/post_process/exposure_finalize.spv");

	PipelinePresets::writeShaderStage(PipelineID::ToneMap, COMPUTE_STAGE, "res/shaders/post_process/tone_map.spv");

	PipelinePresets::writeShaderStage(PipelineID::HiZGen, COMPUTE_STAGE, "res/shaders/core/hi_z_gen.spv");

	PipelinePresets::writeShaderStage(PipelineID::HDRToCubemap, COMPUTE_STAGE, "res/shaders/environment/hdr2cubemap.spv");
	PipelinePresets::writeShaderStage(PipelineID::SpecularPrefilter, COMPUTE_STAGE, "res/shaders/environment/specular_prefilter.spv");
	PipelinePresets::writeShaderStage(PipelineID::DiffuseIrradiance, COMPUTE_STAGE, "res/shaders/environment/diffuse_irradiance.spv");
	PipelinePresets::writeShaderStage(PipelineID::BRDFLUT, COMPUTE_STAGE, "res/shaders/environment/brdf_lut.spv");

	PipelinePresets::writeShaderStage(PipelineID::GTAO, COMPUTE_STAGE, "res/shaders/ao/gtao_main.spv");
	PipelinePresets::writeShaderStage(PipelineID::GTAOFilter, COMPUTE_STAGE, "res/shaders/ao/gtao_filter.spv");
	PipelinePresets::writeShaderStage(PipelineID::GTAOTemporalResolve, COMPUTE_STAGE, "res/shaders/ao/gtao_temporal_resolve.spv");
	PipelinePresets::writeShaderStage(PipelineID::AOUpscale, COMPUTE_STAGE, "res/shaders/ao/ao_upscale.spv");

	PipelinePresets::writeShaderStage(PipelineID::VolumetricLight, COMPUTE_STAGE, "res/shaders/post_process/volumetric_light.spv");
	PipelinePresets::writeShaderStage(PipelineID::VolumetricLightBlur, COMPUTE_STAGE, "res/shaders/post_process/volumetric_light_blur.spv");

	PipelinePresets::writeShaderStage(PipelineID::FlareBright, COMPUTE_STAGE, "res/shaders/post_process/flare_bright.spv");
	PipelinePresets::writeShaderStage(PipelineID::FlareGen, COMPUTE_STAGE, "res/shaders/post_process/flare_gen.spv");

	PipelinePresets::writeShaderStage(PipelineID::SMAAEdges, COMPUTE_STAGE, "res/shaders/post_process/smaa_edges.spv");
	PipelinePresets::writeShaderStage(PipelineID::SMAAWeights, COMPUTE_STAGE, "res/shaders/post_process/smaa_weights.spv");
	PipelinePresets::writeShaderStage(PipelineID::SMAABlend, COMPUTE_STAGE, "res/shaders/post_process/smaa_blend.spv");

	PipelinePresets::writeShaderStage(PipelineID::FXAA, COMPUTE_STAGE, "res/shaders/post_process/fxaa.spv");

	PipelinePresets::writeShaderStage(PipelineID::CMAA2Edges, COMPUTE_STAGE, "res/shaders/post_process/cmaa2_edges.spv");
	PipelinePresets::writeShaderStage(PipelineID::CMAA2ShapeCandidates, COMPUTE_STAGE, "res/shaders/post_process/cmaa2_shape_candidates.spv");
	PipelinePresets::writeShaderStage(PipelineID::CMAA2DeferredResolve, COMPUTE_STAGE, "res/shaders/post_process/cmaa2_deferred_resolve.spv");
	PipelinePresets::writeShaderStage(PipelineID::CMAA2DispatchArgs, COMPUTE_STAGE, "res/shaders/post_process/indirect_args_cmaa2.spv");

	PipelinePresets::writeShaderStage(PipelineID::ClusterTileSliceRanges, COMPUTE_STAGE, "res/shaders/clustered/cluster_tile_slice_ranges.spv");
	PipelinePresets::writeShaderStage(PipelineID::ClusterCount, COMPUTE_STAGE, "res/shaders/clustered/cluster_count.spv");
	PipelinePresets::writeShaderStage(PipelineID::ClusterScanOffsets, COMPUTE_STAGE, "res/shaders/clustered/cluster_scan_offsets.spv");
	PipelinePresets::writeShaderStage(PipelineID::ClusterScatterIDs, COMPUTE_STAGE, "res/shaders/clustered/cluster_scatter_ids.spv");
	PipelinePresets::writeShaderStage(PipelineID::VisibleLightList, COMPUTE_STAGE, "res/shaders/clustered/visible_light_list.spv");
	PipelinePresets::writeShaderStage(PipelineID::IndirectArgsLight, COMPUTE_STAGE, "res/shaders/clustered/indirect_args_light.spv");

	PipelinePresets::writeShaderStage(PipelineID::ScreenSpaceContactShadows, COMPUTE_STAGE, "res/shaders/shadows/bend_sss.spv");

	PipelinePresets::writeShaderStage(PipelineID::Opaque, VERTEX_STAGE, "res/shaders/core/forwardVS.spv");
	PipelinePresets::writeShaderStage(PipelineID::Opaque, FRAGMENT_STAGE, "res/shaders/core/forwardFS.spv");

	PipelinePresets::writeShaderStage(PipelineID::Wireframe, VERTEX_STAGE, "res/shaders/core/forwardVS.spv");
	PipelinePresets::writeShaderStage(PipelineID::Wireframe, FRAGMENT_STAGE, "res/shaders/core/forwardFS.spv");

	PipelinePresets::writeShaderStage(PipelineID::OBBLine, VERTEX_STAGE, "res/shaders/debug/obb_lineVS.spv");
	PipelinePresets::writeShaderStage(PipelineID::OBBLine, FRAGMENT_STAGE, "res/shaders/debug/obb_lineFS.spv");

	PipelinePresets::writeShaderStage(PipelineID::Skybox, VERTEX_STAGE, "res/shaders/environment/skyboxVS.spv");
	PipelinePresets::writeShaderStage(PipelineID::Skybox, FRAGMENT_STAGE, "res/shaders/environment/skyboxFS.spv");


	for (size_t i = 0; i < static_cast<size_t>(PipelineID::Count); ++i) {
		setupShaders(PipelinePresets::pipelinePresetBuilder[i], dq);
	}
}

// graphic pipelines can share the same builder
void PipelinePresets::setupBaseBuilder() {
	_defaultBuilder._pipelineLayout = Pipelines::_globalLayout.layout;

	_defaultBuilder.colorFormats.push_back(VK_FORMAT_B10G11R11_UFLOAT_PACK32);
	//_defaultBuilder.colorFormats.push_back(VK_FORMAT_R16G16B16A16_SFLOAT);
	_defaultBuilder.depthFormat = VK_FORMAT_D32_SFLOAT;
}

void PipelinePresets::createPipeline(
	PipelineBuilder& builder,
	const VkDevice device,
	PipelineID id,
	PipelineCategory type,
	std::string name,
	bool swappable)
{
	PipelinePreset& present = PipelinePresets::getPipelinePresetByID(id);

	PipelineHandle& pipeHdl = Pipelines::getHandle(id);

	if (type == PipelineCategory::Raster) {
		builder.initializePipelineSTypes();
		// Defaults to primary builder formats,
		// can overwrite the format if wanted
		if (present.colorFormats.empty() && present.depthFormat == VK_FORMAT_UNDEFINED) {
			present.colorFormats = builder.colorFormats;
			present.depthFormat = builder.depthFormat;
		}
		PipelineManager::setupPipelineConfig(builder, present);

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

	PipelinePresets::setupBaseBuilder();
}


void PipelineManager::initPipelines(DeletionQueue& queue) {
	DeletionQueue shaderDeletionQ;

	initPipelineShaders(shaderDeletionQ);

	const auto device = Backend::getDevice();

	// === OPAQUE PIPELINE ===
	PipelinePreset& opaquePreset = PipelinePresets::getPipelinePresetByID(PipelineID::Opaque);
	opaquePreset.cullMode = VK_CULL_MODE_BACK_BIT;
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::Opaque,
		PipelineCategory::Raster,
		"Opaque",
		false);

	// === WIREFRAME PIPELINE ===
	PipelinePreset& wirePreset = PipelinePresets::getPipelinePresetByID(PipelineID::Wireframe);
	wirePreset.polygonMode = VK_POLYGON_MODE_LINE;
	wirePreset.depthCompareOp = VK_COMPARE_OP_GREATER;
	wirePreset.enableDepthWrite = true;
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::Wireframe,
		PipelineCategory::Raster,
		"Wireframe",
		true);

	// === OBB LINE DEBUG PIPELINE ===
	PipelinePreset& lineDebugPreset = PipelinePresets::getPipelinePresetByID(PipelineID::OBBLine);
	lineDebugPreset.polygonMode = VK_POLYGON_MODE_LINE;
	lineDebugPreset.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	lineDebugPreset.enableDepthWrite = true;
	lineDebugPreset.depthCompareOp = VK_COMPARE_OP_GREATER;
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::OBBLine,
		PipelineCategory::Raster,
		"OBBLine",
		false);

	// === SKYBOX PIPELINE ===
	PipelinePreset& skyboxPreset = PipelinePresets::getPipelinePresetByID(PipelineID::Skybox);
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::Skybox,
		PipelineCategory::Raster,
		"Skybox",
		false);

	// === TRANSPARENT PIPELINE ===
	PipelinePreset& transparentPreset = PipelinePresets::getPipelinePresetByID(PipelineID::Transparent);
	transparentPreset.colorFormats.push_back(VK_FORMAT_R16G16B16A16_SFLOAT);
	//transparentPreset.colorFormats.push_back(VK_FORMAT_R16G16B16A16_SFLOAT);
	transparentPreset.depthFormat = VK_FORMAT_D32_SFLOAT;
	transparentPreset.enableBlending = true;
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::Transparent,
		PipelineCategory::Raster,
		"Transparent");

	// === PREPASS PIPELINE ===
	PipelinePreset& prepassPreset = PipelinePresets::getPipelinePresetByID(PipelineID::Prepass);
	prepassPreset.colorFormats.push_back(VK_FORMAT_A2B10G10R10_UNORM_PACK32); // Normals
	prepassPreset.colorFormats.push_back(VK_FORMAT_R16G16_SFLOAT);            // Velocity
	prepassPreset.depthFormat = VK_FORMAT_D32_SFLOAT;
	prepassPreset.depthCompareOp = VK_COMPARE_OP_GREATER;
	prepassPreset.cullMode = VK_CULL_MODE_BACK_BIT;
	prepassPreset.enableDepthWrite = true;
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::Prepass,
		PipelineCategory::Raster,
		"Prepass");

	// === SHADOW PIPELINE ===
	PipelinePreset& csmPreset = PipelinePresets::getPipelinePresetByID(PipelineID::Shadow);
	csmPreset.depthFormat = VK_FORMAT_D32_SFLOAT;
	csmPreset.depthCompareOp = VK_COMPARE_OP_LESS;
	csmPreset.cullMode = VK_CULL_MODE_FRONT_BIT;
	csmPreset.enableDepthWrite = true;

	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::Shadow,
		PipelineCategory::Raster,
		"Shadow");

	// === COMPUTE PIPELINE SETUP STAGE ===
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,PipelineID::ToneMap,
		PipelineCategory::Compute,
		"ToneMap");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::ExposureReduce,
		PipelineCategory::Compute,
		"ExposureReduce");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::ExposureFinalize,
		PipelineCategory::Compute,
		"ExposureFinalize");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::FlareBright,
		PipelineCategory::Compute,
		"FlareBright");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::FlareGen,
		PipelineCategory::Compute,
		"FlareGen");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::HDRToCubemap,
		PipelineCategory::Compute,
		"HDRToCubemap");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::SpecularPrefilter,
		PipelineCategory::Compute,
		"SpecularPrefilter");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::DiffuseIrradiance,
		PipelineCategory::Compute,
		"DiffuseIrradiance");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::BRDFLUT,
		PipelineCategory::Compute,
		"BRDFLUT");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::GTAO,
		PipelineCategory::Compute,
		"GTAO");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::GTAOFilter,
		PipelineCategory::Compute,
		"GTAOFilter");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::GTAOTemporalResolve,
		PipelineCategory::Compute,
		"GTAOTemporalResolve");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::HiZGen,
		PipelineCategory::Compute,
		"HiZGen");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::VolumetricLight,
		PipelineCategory::Compute,
		"VolumetricLight");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::VolumetricLightBlur,
		PipelineCategory::Compute,
		"VolumetricLightBlur");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::ClusterTileSliceRanges,
		PipelineCategory::Compute,
		"ClusterTileSliceRanges");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::VisibleLightList,
		PipelineCategory::Compute,
		"VisibleLightList");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::IndirectArgsLight,
		PipelineCategory::Compute,
		"IndirectArgsLight");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::ClusterCount,
		PipelineCategory::Compute,
		"ClusterCount");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::ClusterScanOffsets,
		PipelineCategory::Compute,
		"ClusterScanOffsets");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::ClusterScatterIDs,
		PipelineCategory::Compute,
		"ClusterScatterIDs");

	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::ScreenSpaceContactShadows,
		PipelineCategory::Compute,
		"ScreenSpaceContactShadows");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::AOUpscale,
		PipelineCategory::Compute,
		"AOUpscale");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::SMAAEdges,
		PipelineCategory::Compute,
		"SMAAEdges");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::SMAAWeights,
		PipelineCategory::Compute,
		"SMAAWeights");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::SMAABlend,
		PipelineCategory::Compute,
		"SMAABlend");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::CMAA2Edges,
		PipelineCategory::Compute,
		"CMAA2Edges");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::CMAA2ShapeCandidates,
		PipelineCategory::Compute,
		"CMAA2ShapeCandidates");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::CMAA2DeferredResolve,
		PipelineCategory::Compute,
		"CMAA2DeferredResolve");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::CMAA2DispatchArgs,
		PipelineCategory::Compute,
		"CMAA2DispatchArgs");
	PipelinePresets::createPipeline(
		PipelinePresets::_defaultBuilder,
		device,
		PipelineID::FXAA,
		PipelineCategory::Compute,
		"FXAA");

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

void PipelineManager::setupPipelineConfig(PipelineBuilder& pipeline, PipelinePreset& settings) {

	PipelineConfigs::inputAssemblyConfig(pipeline._inputAssembly, settings.topology, VK_FALSE);

	if (settings.enableDepthBias) {
		pipeline._rasterizer.depthBiasEnable = VK_TRUE;
		pipeline._rasterizer.depthBiasConstantFactor = settings.depthBiasConstant;
		pipeline._rasterizer.depthBiasSlopeFactor = settings.depthBiasSlope;
		pipeline._rasterizer.depthBiasClamp = settings.depthBiasClamp;
	}
	PipelineConfigs::rasterizerConfig(pipeline._rasterizer, settings.polygonMode, 1.0f, settings.cullMode, settings.frontFace);

	PipelineConfigs::multisamplingConfig(pipeline._multisampling, VK_FALSE);

	PipelineConfigs::colorBlendingConfig(pipeline._colorBlendAttachment,
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
		settings.enableBlending, VK_BLEND_FACTOR_ONE);

	PipelineConfigs::depthStencilConfig(
		pipeline._depthStencil,
		settings.enableDepthTest,
		settings.enableDepthWrite,
		VK_FALSE,
		VK_FALSE,
		settings.depthCompareOp);

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
	bool sampleShadingEnabled)
{
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	multisampling.sampleShadingEnable = sampleShadingEnabled;
	multisampling.minSampleShading = 1.0f;
	multisampling.pSampleMask = nullptr;

	multisampling.alphaToCoverageEnable = VK_FALSE;
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
