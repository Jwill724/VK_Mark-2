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
	reg(RP::PrepassMesh,       {{ RS::MeshletCull_t,    SS::TASK_STAGE     },
								{ RS::Prepass_m,        SS::MESH_STAGE     },
								{ RS::Prepass_f,        SS::FRAGMENT_STAGE }});

	reg(RP::PrepassMaskedMesh, {{ RS::MeshletCull_t,    SS::TASK_STAGE     },
								{ RS::PrepassMasked_m,  SS::MESH_STAGE     },
								{ RS::PrepassMasked_f,  SS::FRAGMENT_STAGE }});

	reg(RP::ShadowMesh,  {{ RS::Shadow_t,  SS::TASK_STAGE },
						  { RS::Shadow_m,  SS::MESH_STAGE }});

	reg(RP::ShadowMeshMaskedD32, { { RS::Shadow_t,        SS::TASK_STAGE },
								   { RS::ShadowMasked_m,  SS::MESH_STAGE },
								   { RS::ShadowMasked_f,  SS::FRAGMENT_STAGE } });

	reg(RP::ShadowMeshMaskedD16, { { RS::Shadow_t,        SS::TASK_STAGE },
								   { RS::ShadowMasked_m,  SS::MESH_STAGE },
								   { RS::ShadowMasked_f,  SS::FRAGMENT_STAGE } });

	reg(RP::WireframeMesh, {{ RS::MeshletCull_t,   SS::TASK_STAGE },
							{ RS::Wireframe_m,     SS::MESH_STAGE },
							{ RS::Wireframe_f,     SS::FRAGMENT_STAGE }});

	reg(RP::Skybox,      {{ RS::Skybox_v,      SS::VERTEX_STAGE   },
						  { RS::Skybox_f,      SS::FRAGMENT_STAGE }});

	reg(RP::TransparentForward, {{ RS::TransparentCull_t, SS::TASK_STAGE },
								 { RS::TransparentDraw_m, SS::MESH_STAGE },
								 { RS::Transparent_f, SS::FRAGMENT_STAGE }});

	reg(RP::LineDebug,   {{ RS::LineDebug_v,     SS::VERTEX_STAGE   },
						  { RS::LineDebug_f,     SS::FRAGMENT_STAGE }});

	// === Compute ===
	auto regC = [&](RP id, RS shader) {
		reg(id, {{ shader, SS::COMPUTE_STAGE }});
	};

	regC(RP::MaterialResolve,           RS::MaterialResolve_c);
	regC(RP::VelocityResolve,           RS::VelocityResolve_c);
	regC(RP::OpaqueLighting,            RS::OpaqueLighting_c);
	regC(RP::HDRSceneComposite,         RS::HDRSceneComposite_c);
	regC(RP::ExposureReduce,            RS::ExposureReduce_c);
	regC(RP::ExposureFinalize,          RS::ExposureFinalize_c);
	regC(RP::FinalComposite,            RS::FinalComposite_c);
	regC(RP::HiZGen,                    RS::HiZGen_c);
	regC(RP::HDRToCubemap,              RS::HDRToCubemap_c);
	regC(RP::SpecularPrefilter,         RS::SpecularPrefilter_c);
	regC(RP::SHIrradiance,              RS::SHIrradiance_c);
	regC(RP::BRDFLUT,                   RS::BRDFLUT_c);
	regC(RP::VBGI,                      RS::VBGI_c);
	regC(RP::BilateralUpsample,         RS::BilateralUpsample_c);
	regC(RP::GIAccumulate,              RS::GIAccumulate_c);
	regC(RP::AODenoise,                 RS::AODenoise_c);
	regC(RP::GIDenoise,                 RS::GIDenoise_c);
	regC(RP::HiZPrefilter,              RS::HiZPrefilter_c);
	regC(RP::VolumetricLight,           RS::VolumetricLight_c);
	regC(RP::VolumetricLightBlur,       RS::VolumetricLightBlur_c);
	regC(RP::VolumetricLightResolve,    RS::VolumetricLightResolve_c);
	regC(RP::FlareBright,               RS::FlareBright_c);
	regC(RP::FlareGen,                  RS::FlareGen_c);
	regC(RP::BloomDownsample,           RS::BloomDownsample_c);
	regC(RP::BloomUpsample,             RS::BloomUpsample_c);
	regC(RP::TAA,                       RS::TAA_c);
	regC(RP::TransparentClusterBounds,  RS::TransparentClusterBounds_c);
	regC(RP::ClusterTileSliceRanges,    RS::ClusterTileSliceRanges_c);
	regC(RP::ClusterCount,              RS::ClusterCount_c);
	regC(RP::ClusterScanOffsets,        RS::ClusterScanOffsets_c);
	regC(RP::ClusterScatterIDs,         RS::ClusterScatterIDs_c);
	regC(RP::LightCull,                 RS::LightCulling_c);
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
	regC(RP::GBufferDebug,              RS::GBufferDebug_c);
	regC(RP::NRDPrepare,                RS::NRDPrepare_c);

	regC(RP::TlasInstances,             RS::TlasInstances_c);
	regC(RP::RTRayArgs,                 RS::RTRayArgs_c);
	regC(RP::RTShadowTrace,             RS::RTShadowTrace_c);
	regC(RP::ReflectClassify,           RS::ReflectClassify_c);
	regC(RP::RTReflectTrace,            RS::RTReflectTrace_c);

	// === Presets ===
	auto& wireMesh = m_pipelinePresets[static_cast<size_t>(RP::WireframeMesh)];
	wireMesh.polygonMode      = VK_POLYGON_MODE_LINE;
	wireMesh.depthCompareOp   = VK_COMPARE_OP_GREATER;
	wireMesh.cullMode         = VK_CULL_MODE_BACK_BIT;
	wireMesh.enableDepthWrite = true;

	// Adjust for _EQUAL of Prepass
	wireMesh.enableDepthBias = true;
	wireMesh.depthBiasConstant = 1.75f;
	wireMesh.depthBiasSlope = 1.25f;

	m_pipelinePresets[static_cast<size_t>(RP::LineDebug)].polygonMode      = VK_POLYGON_MODE_LINE;
	m_pipelinePresets[static_cast<size_t>(RP::LineDebug)].topology         = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	m_pipelinePresets[static_cast<size_t>(RP::LineDebug)].enableDepthWrite = true;
	m_pipelinePresets[static_cast<size_t>(RP::LineDebug)].depthCompareOp   = VK_COMPARE_OP_GREATER;

	auto& prepass                    = m_pipelinePresets[static_cast<size_t>(RP::PrepassMesh)];
	prepass.colorFormats             = { Vulkan_Format::RG32U, Vulkan_Format::RG8unorm };
	prepass.depthFormat              = Vulkan_Format::D32;
	prepass.depthCompareOp           = VK_COMPARE_OP_GREATER;
	prepass.cullMode                 = VK_CULL_MODE_BACK_BIT;
	prepass.enableDepthWrite         = true;

	auto& shadow                     = m_pipelinePresets[static_cast<size_t>(RP::ShadowMesh)];
	shadow.depthFormat               = Vulkan_Format::D32;
	shadow.depthCompareOp            = VK_COMPARE_OP_LESS;
	shadow.cullMode                  = VK_CULL_MODE_NONE;
	shadow.enableDepthWrite          = true;

	m_pipelinePresets[static_cast<size_t>(RP::ShadowMeshMaskedD32)] = shadow;

	m_pipelinePresets[static_cast<size_t>(RP::ShadowMeshMaskedD16)] = shadow;
	m_pipelinePresets[static_cast<size_t>(RP::ShadowMeshMaskedD16)].depthFormat = Vulkan_Format::D16;

	// Cone culling is skipped for double sided
	auto& prepassMasked = m_pipelinePresets[static_cast<size_t>(RP::PrepassMaskedMesh)];
	prepassMasked = prepass;
	prepassMasked.cullMode = VK_CULL_MODE_NONE;

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

	VkPipelineColorBlendAttachmentState velocityAccumBlend{};
	velocityAccumBlend.blendEnable = VK_TRUE;
	velocityAccumBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT;
	velocityAccumBlend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	velocityAccumBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
	velocityAccumBlend.colorBlendOp = VK_BLEND_OP_ADD;
	velocityAccumBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	velocityAccumBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	velocityAccumBlend.alphaBlendOp = VK_BLEND_OP_ADD;


	auto& transparent                = m_pipelinePresets[static_cast<size_t>(RP::TransparentForward)];
	transparent.colorFormats         = { Vulkan_Format::RGBA16F, Vulkan_Format::R16F, Vulkan_Format::RG16F };
	transparent.depthFormat          = Vulkan_Format::D32;
	transparent.enableBlending       = true;
	transparent.depthCompareOp       = VK_COMPARE_OP_GREATER;
	transparent.blendAttachments     = { accumBlend, revealBlend, velocityAccumBlend };
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
	TheBuilder.SetFormats(static_cast<VkFormat>(Vulkan_Format::BGRpacked), static_cast<VkFormat>(Vulkan_Format::D32));

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
		if (stage == Vulkan_ShaderStage::VERTEX_STAGE ||
			stage == Vulkan_ShaderStage::FRAGMENT_STAGE ||
			stage == Vulkan_ShaderStage::TASK_STAGE ||
			stage == Vulkan_ShaderStage::MESH_STAGE)
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
