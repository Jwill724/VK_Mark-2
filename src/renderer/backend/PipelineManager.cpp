#include "pch.h"

#include "PipelineManager.h"
#include "PipelineBuilder.h"

using PM = PipelineManager;

static PipelineBuilder TheBuilder;

void PM::RegisterPipelines()
{
	using RS = RD::Renderer_Shader;
	using RP = RD::Renderer_Pipeline;
	using SS = Vulkan_ShaderStage;

	// Wires shader enums to a pipeline, paths are resolved inside Shader's constructor
	auto reg = [&](RP id, std::initializer_list<std::pair<RS, SS>> shaders) {
		auto& p = m_pipelines[static_cast<size_t>(id)];
		p = Pipeline(id);
		//p.Handle().debugName = "Generic_Pipeline";
		for (auto [shader, stage] : shaders)
			p.AddShader(shader, stage);
	};

	// === Graphics ===
	reg(RP::Opaque,      {{ RS::Opaque_v,      SS::VERTEX_STAGE   },
						  { RS::Opaque_f,      SS::FRAGMENT_STAGE }});

	reg(RP::Wireframe,   {{ RS::Wireframe_v,   SS::VERTEX_STAGE   },
						  { RS::Wireframe_f,   SS::FRAGMENT_STAGE }});

	reg(RP::Prepass,     {{ RS::Prepass_v,     SS::VERTEX_STAGE   },
						  { RS::Prepass_f,     SS::FRAGMENT_STAGE }});

	reg(RP::Shadow,      {{ RS::Shadow_v,      SS::VERTEX_STAGE   }});

	reg(RP::Skybox,      {{ RS::Skybox_v,      SS::VERTEX_STAGE   },
						  { RS::Skybox_f,      SS::FRAGMENT_STAGE }});

	reg(RP::Transparent, {{ RS::Opaque_v,      SS::VERTEX_STAGE   },
						  { RS::Transparent_f, SS::FRAGMENT_STAGE }});

	reg(RP::LineDebug,   {{ RS::LineDebug_v,     SS::VERTEX_STAGE   },
						  { RS::LineDebug_f,     SS::FRAGMENT_STAGE }});

	// === Compute ===
	auto regC = [&](RP id, RS shader) {
		reg(id, {{ shader, SS::COMPUTE_STAGE }});
	};

	regC(RP::TransparentResolve,        RS::TransparentResolve_c);
	regC(RP::ExposureReduce,            RS::ExposureReduce_c);
	regC(RP::ExposureFinalize,          RS::ExposureFinalize_c);
	regC(RP::FinalComposite,            RS::FinalComposite_c);
	regC(RP::HiZGen,                    RS::HiZGen_c);
	regC(RP::HDRToCubemap,              RS::HDRToCubemap_c);
	regC(RP::SpecularPrefilter,         RS::SpecularPrefilter_c);
	regC(RP::DiffuseIrradiance,         RS::DiffuseIrradiance_c);
	regC(RP::BRDFLUT,                   RS::BRDFLUT_c);
	regC(RP::SSAO,                      RS::SSAO_c);
	regC(RP::SSAOFilter,                RS::SSAOFilter_c);
	regC(RP::SSAODenoise,               RS::SSAODenoise_c);
	regC(RP::SSAODepthPrefilter,        RS::SSAODepthPrefilter_c);
	regC(RP::VolumetricLight,           RS::VolumetricLight_c);
	regC(RP::VolumetricLightBlur,       RS::VolumetricLightBlur_c);
	regC(RP::FlareBright,               RS::FlareBright_c);
	regC(RP::FlareGen,                  RS::FlareGen_c);
	regC(RP::BloomDownsample,           RS::BloomDownsample_c);
	regC(RP::BloomUpsample,             RS::BloomUpsample_c);
	regC(RP::SMAAEdges,                 RS::SMAAEdges_c);
	regC(RP::SMAAWeights,               RS::SMAAWeights_c);
	regC(RP::SMAABlend,                 RS::SMAABlend_c);
	regC(RP::FXAA,                      RS::FXAA_c);
	regC(RP::TAA,                       RS::TAA_c);
	regC(RP::CMAA2Edges,                RS::CMAA2Edges_c);
	regC(RP::CMAA2ShapeCandidates,      RS::CMAA2ShapeCandidates_c);
	regC(RP::CMAA2DeferredResolve,      RS::CMAA2DeferredResolve_c);
	regC(RP::CMAA2DispatchArgs,         RS::CMAA2DispatchArgs_c);
	regC(RP::ClusterTileSliceRanges,    RS::ClusterTileSliceRanges_c);
	regC(RP::ClusterCount,              RS::ClusterCount_c);
	regC(RP::ClusterScanOffsets,        RS::ClusterScanOffsets_c);
	regC(RP::ClusterScatterIDs,         RS::ClusterScatterIDs_c);
	regC(RP::LightCulling,              RS::LightCulling_c);
	regC(RP::IndirectArgsLight,         RS::IndirectArgsLight_c);
	regC(RP::ScreenSpaceContactShadows, RS::ScreenSpaceContactShadows_c);
	regC(RP::ShadowBounds,              RS::ShadowBounds_c);
	regC(RP::ChromaticAberration,       RS::ChromaticAberration_c);
	regC(RP::InstanceCull,              RS::InstanceCull_c);
	regC(RP::DrawArgs,                  RS::DrawArgs_c);
	regC(RP::DrawEmit,                  RS::DrawEmit_c);
	regC(RP::DrawScatter,               RS::DrawScatter_c);
	regC(RP::DrawPlace,                 RS::DrawPlace_c);
	regC(RP::DebugCount,                RS::DebugCount_c);
	regC(RP::DebugArgs,                 RS::DebugArgs_c);
	regC(RP::DebugBuild,                RS::DebugBuild_c);

	// === Presets ===
	m_pipelinePresets[static_cast<size_t>(RP::Opaque)].cullMode            = VK_CULL_MODE_BACK_BIT;

	m_pipelinePresets[static_cast<size_t>(RP::Wireframe)].polygonMode      = VK_POLYGON_MODE_LINE;
	m_pipelinePresets[static_cast<size_t>(RP::Wireframe)].depthCompareOp   = VK_COMPARE_OP_GREATER;
	m_pipelinePresets[static_cast<size_t>(RP::Wireframe)].enableDepthWrite = true;

	m_pipelinePresets[static_cast<size_t>(RP::LineDebug)].polygonMode      = VK_POLYGON_MODE_LINE;
	m_pipelinePresets[static_cast<size_t>(RP::LineDebug)].topology         = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	m_pipelinePresets[static_cast<size_t>(RP::LineDebug)].enableDepthWrite = true;
	m_pipelinePresets[static_cast<size_t>(RP::LineDebug)].depthCompareOp   = VK_COMPARE_OP_GREATER;

	auto& prepass                    = m_pipelinePresets[static_cast<size_t>(RP::Prepass)];
	prepass.colorFormats             = { Vulkan_Format::RG32U, Vulkan_Format::ABGRpacked, Vulkan_Format::RG16F };
	prepass.depthFormat              = Vulkan_Format::D32;
	prepass.depthCompareOp           = VK_COMPARE_OP_GREATER;
	prepass.cullMode                 = VK_CULL_MODE_BACK_BIT;
	prepass.enableDepthWrite         = true;

	auto& shadow                     = m_pipelinePresets[static_cast<size_t>(RP::Shadow)];
	shadow.depthFormat               = Vulkan_Format::D32;
	shadow.depthCompareOp            = VK_COMPARE_OP_LESS;
	shadow.cullMode                  = VK_CULL_MODE_FRONT_BIT;
	shadow.enableDepthWrite          = true;

	VkPipelineColorBlendAttachmentState accumBlend{};
	accumBlend.blendEnable         = VK_TRUE;
	accumBlend.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
									  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	accumBlend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	accumBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
	accumBlend.colorBlendOp        = VK_BLEND_OP_ADD;
	accumBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	accumBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	accumBlend.alphaBlendOp        = VK_BLEND_OP_ADD;

	VkPipelineColorBlendAttachmentState revealBlend{};
	revealBlend.blendEnable         = VK_TRUE;
	revealBlend.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT;
	revealBlend.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
	revealBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
	revealBlend.colorBlendOp        = VK_BLEND_OP_ADD;
	revealBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	revealBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	revealBlend.alphaBlendOp        = VK_BLEND_OP_ADD;

	auto& transparent                = m_pipelinePresets[static_cast<size_t>(RP::Transparent)];
	transparent.colorFormats         = { Vulkan_Format::RGBA16F, Vulkan_Format::R16F };
	transparent.depthFormat          = Vulkan_Format::D32;
	transparent.enableBlending       = true;
	transparent.depthCompareOp       = VK_COMPARE_OP_GREATER;
	transparent.blendAttachments     = { accumBlend, revealBlend };
}

// All pipelines use this one layout
void PM::CreatePipelineLayout(
	VkDevice device,
	const std::vector<VkDescriptorSetLayout>& descriptorLayouts)
{
	ASSERT(descriptorLayouts.size() == static_cast<size_t>(RD::DescriptorSlot::Count));

	PushConstantDef pcDef { 0, RD::MAX_PUSH_CONSTANT_SIZE, static_cast<VkShaderStageFlags>(Vulkan_ShaderStage::ALL_STAGES) };
	VkPipelineLayout pipelineLayout;

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.flags          = 0;
	pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorLayouts.size());
	pipelineLayoutInfo.pSetLayouts    = descriptorLayouts.data();

	VkPushConstantRange pushConstantRange{};
	pushConstantRange.stageFlags              = pcDef.stageFlags;
	pushConstantRange.offset                  = pcDef.offset;
	pushConstantRange.size                    = pcDef.size;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges    = &pushConstantRange;

	VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

	m_globalLayout.pipelineLayout = std::move(pipelineLayout);
	m_globalLayout.pushConstantDef = std::move(pcDef);
}

void PM::InitPipelines(VkDevice device)
{
	ASSERT(m_globalLayout.pipelineLayout != VK_NULL_HANDLE);
	ASSERT(device != VK_NULL_HANDLE);

	TheBuilder.SetPipelineLayout(m_globalLayout.pipelineLayout);
	TheBuilder.InitCreateInfoStructs();

	// Default m_image formats
	TheBuilder.SetFormats(static_cast<VkFormat>(Vulkan_Format::RGBA16F), static_cast<VkFormat>(Vulkan_Format::D32));

	RegisterPipelines();

	for (auto& pipeline : m_pipelines)
	{
		PipelinePreset& preset = m_pipelinePresets[static_cast<size_t>(pipeline.ID())];
		PipelineHandle& handle = pipeline.Handle();
		handle.layout = m_globalLayout;

		if (handle.bindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS)
		{
			TheBuilder.InitCreateInfoStructs();
			SetupPipelineConfig(preset);

			// Only raster pipelines will get topology info
			handle.topology = preset.topology;
		}

		pipeline.Build(TheBuilder, preset, device);
	}
}

void PM::SetupPipelineConfig(const PipelinePreset& preset)
{
	TheBuilder.InputAssemblyConfig(preset.topology);

	if (preset.enableDepthBias)
	{
		TheBuilder.DepthBiasConfig(preset.depthBiasConstant, preset.depthBiasSlope);
	}

	TheBuilder.RasterizerConfig(preset.polygonMode, preset.cullMode, preset.frontFace);

	TheBuilder.MultisamplingConfig();

	TheBuilder.ColorBlendingConfig(
		preset.blendAttachments,
		VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
		preset.enableBlending,
		VK_BLEND_FACTOR_ONE);

	const bool hasColor = preset.IsColorDefined();
	const bool hasDepth = preset.IsDepthDefined();

	if (hasColor || hasDepth)
	{
		// At least one was set intentionally
		TheBuilder.ColorAndDepthConfig(
			preset.colorFormats,
			hasDepth ? preset.depthFormat
					 : static_cast<Vulkan_Format>(TheBuilder.GetDepthFormat()));
	}
	else
	{
		// Nothing specified at all — fall back to builder defaults.
		TheBuilder.ColorAndDepthConfig(
			{ static_cast<Vulkan_Format>(TheBuilder.GetColorFormat()) },
			static_cast<Vulkan_Format>(TheBuilder.GetDepthFormat()));
	}

	TheBuilder.DepthStencilConfig(
		preset.enableDepthTest,
		preset.enableDepthWrite,
		preset.depthCompareOp);
}

void PM::TickFrame(VkDevice device, uint32_t completedFrame, uint32_t framesInFlight)
{
	m_currentFrame = completedFrame + 1;

	m_retiredPipelines.erase(
		std::remove_if(m_retiredPipelines.begin(), m_retiredPipelines.end(),
			[&](const RetiredPipeline& r) {
				if (r.frameIndex + framesInFlight <= completedFrame) {
					vkDestroyPipeline(device, r.pipeline, nullptr);
					return true;
				}
				return false;
			}),
		m_retiredPipelines.end());
}

bool PM::Rebuild(RD::Renderer_Pipeline id, VkDevice device)
{
	size_t i = static_cast<size_t>(id);

	// Snapshot the old pipeline before build overwrites it
	VkPipeline oldPipeline = m_pipelines[i].Handle().pipeline;

	TheBuilder.SetPipelineLayout(m_globalLayout.pipelineLayout);
	TheBuilder.InitCreateInfoStructs();

	if (m_pipelines[i].Handle().bindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS)
	{
		TheBuilder.InitCreateInfoStructs();
		SetupPipelineConfig(m_pipelinePresets[i]);
	}

	if (!m_pipelines[i].Build(TheBuilder, m_pipelinePresets[i], device))
		return false; // build failed, old pipeline untouched and still live

	// Build succeeded — retire the old one, keep what Build wrote
	if (oldPipeline != VK_NULL_HANDLE)
		m_retiredPipelines.push_back({ oldPipeline, m_currentFrame });

	return true;
}

void PM::Shutdown(VkDevice device)
{
	// Destroy all live pipelines
	for (auto& pipeline : m_pipelines)
	{
		if (pipeline.Handle().pipeline != VK_NULL_HANDLE)
		{
			vkDestroyPipeline(device, pipeline.Handle().pipeline, nullptr);
			pipeline.Handle().pipeline = VK_NULL_HANDLE;
		}
	}

	for (auto& r : m_retiredPipelines)
		vkDestroyPipeline(device, r.pipeline, nullptr);
	m_retiredPipelines.clear();

	vkDestroyPipelineLayout(device, m_globalLayout.pipelineLayout, nullptr);
}

// -------------------------
// Pipeline class functions
// -------------------------

PM::Pipeline& PM::Pipeline::AddShader(RD::Renderer_Shader shader, Vulkan_ShaderStage stage)
{
	// First time definition of pipeline type
	if (m_handle.bindPoint == VK_PIPELINE_BIND_POINT_MAX_ENUM)
	{
		if (stage == Vulkan_ShaderStage::VERTEX_STAGE || stage == Vulkan_ShaderStage::FRAGMENT_STAGE)
		{
			m_handle.bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		}
		else // Compute stage
		{
			m_handle.bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
		}
	}
	m_shaders.emplace_back(shader, stage);
	return *this;
}

bool PipelineManager::Pipeline::Build(PipelineBuilder& builder, PipelinePreset& preset, VkDevice device)
{
	ASSERT(!m_shaders.empty()); // Call AddShader() first

	// Modules are stack-local and lifetime controlled here
	std::vector<VkShaderModule> modules;
	modules.reserve(m_shaders.size());

	preset.shaderStages.clear();
	preset.shaderStages.reserve(m_shaders.size());

	auto DestroyModules = [&](void)
	{
		for (VkShaderModule module : modules)
			vkDestroyShaderModule(device, module, nullptr);
	};

	for (auto& shader : m_shaders)
	{
		VkShaderModule mod = VK_NULL_HANDLE;
		if (!shader.CreateModule(device, mod))
		{
			DestroyModules(); // only cleans up previously successful modules
			return false;
		}
		modules.push_back(mod);
		preset.shaderStages.push_back(shader.MakeStageInfo(mod));
	}

	m_handle.pipeline = VK_NULL_HANDLE;
	bool ok = builder.CreatePipeline(m_handle, preset, device);

	DestroyModules();
	preset.shaderStages.clear();

	return ok;
}
