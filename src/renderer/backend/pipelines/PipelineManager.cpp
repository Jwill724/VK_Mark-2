#include "pch.h"

#include "PipelineManager.h"
#include "PipelineBuilder.h"
#include <fstream>

using PM = PipelineManager;

static PipelineBuilder TheBuilder;

static const std::string kShaderRoot = "res/shaders/";

VkShaderModule PM::AcquireModule(const char* path, VkDevice device, ModuleCache& cache)
{
	if (auto it = cache.find(path); it != cache.end())
		return it->second;

	std::ifstream file(kShaderRoot + path, std::ios::ate | std::ios::binary);
	if (!file.is_open())
	{
		fmt::println(stderr, "[Pipeline] shader not found: {}{}", kShaderRoot, path);
		return VK_NULL_HANDLE;
	}

	const size_t fileSize = static_cast<size_t>(file.tellg());
	std::vector<uint32_t> spirv(fileSize / sizeof(uint32_t));
	file.seekg(0);
	file.read(reinterpret_cast<char*>(spirv.data()), fileSize);
	file.close();

	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;
	createInfo.codeSize = spirv.size() * sizeof(uint32_t);
	createInfo.pCode = spirv.data();

	VkShaderModule mod = VK_NULL_HANDLE;
	VK_CHECK(vkCreateShaderModule(device, &createInfo, nullptr, &mod));

	cache.emplace(path, mod);
	return mod;
}

void PM::ReleaseModules(VkDevice device, ModuleCache& cache)
{
	for (auto& [path, mod] : cache)
		vkDestroyShaderModule(device, mod, nullptr);
	cache.clear();
}

// All pipelines use this one layout
void PM::CreatePipelineLayout(
	VkDevice device,
	const std::vector<VkDescriptorSetLayout>& descriptorLayouts)
{
	ASSERT(descriptorLayouts.size() == static_cast<size_t>(RD::DescriptorSlot::Count));

	PushConstantDef pcDef{ 0, RD::MAX_PUSH_CONSTANT_SIZE, static_cast<VkShaderStageFlags>(Vulkan_ShaderStage::ALL_STAGES) };
	VkPipelineLayout pipelineLayout;

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.flags = 0;
	pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorLayouts.size());
	pipelineLayoutInfo.pSetLayouts = descriptorLayouts.data();

	VkPushConstantRange pushConstantRange{};
	pushConstantRange.stageFlags = pcDef.stageFlags;
	pushConstantRange.offset = pcDef.offset;
	pushConstantRange.size = pcDef.size;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

	VK_CHECK(vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout));

	m_globalLayout.pipelineLayout = pipelineLayout;
	m_globalLayout.pushConstantDef = pcDef;
}

void PM::InitPipelines(VkDevice device)
{
	ASSERT(m_globalLayout.pipelineLayout != VK_NULL_HANDLE);
	ASSERT(device != VK_NULL_HANDLE);

	PipelineTable::Build();

	TheBuilder.SetPipelineLayout(m_globalLayout.pipelineLayout);

	ModuleCache cache;

	for (size_t i = 0; i < RD::PIPELINE_COUNT; ++i)
	{
		const auto id = static_cast<RD::Renderer_Pipeline>(i);
		const PipelineDef& def = PipelineTable::Get(id);

		Pipeline& pipeline = m_pipelines[i];
		pipeline.Init(id);

		PipelineHandle& handle = pipeline.Handle();
		handle.layout = m_globalLayout;
		handle.bindPoint = def.bindPoint;
		handle.debugName = def.DebugName();

		TheBuilder.InitCreateInfoStructs();

		if (def.IsGraphics())
		{
			SetupPipelineConfig(def);

			// Only raster pipelines will get topology info
			handle.topology = def.topology;
		}

		INVARIANT(pipeline.Build(TheBuilder, def, device, cache));
	}

	ReleaseModules(device, cache);
}

void PM::SetupPipelineConfig(const PipelineDef& def)
{
	TheBuilder.InputAssemblyConfig(def.topology);

	if (def.biasConstant != 0.0f || def.biasSlope != 0.0f)
		TheBuilder.DepthBiasConfig(def.biasConstant, def.biasSlope);

	TheBuilder.RasterizerConfig(def.polygon, def.cull, def.frontFace);

	TheBuilder.MultisamplingConfig();

	TheBuilder.AttachmentConfig(def.color, def.blend, def.colorCount, def.depth);

	TheBuilder.DepthStencilConfig(def.depthTest, def.depthWrite, def.depthCompare);
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
	const size_t i = static_cast<size_t>(id);
	const PipelineDef& def = PipelineTable::Get(id);

	Pipeline& pipeline = m_pipelines[i];

	// Snapshot the old pipeline before build overwrites it
	const VkPipeline oldPipeline = pipeline.Handle().pipeline;

	TheBuilder.SetPipelineLayout(m_globalLayout.pipelineLayout);
	TheBuilder.InitCreateInfoStructs();

	if (def.IsGraphics())
		SetupPipelineConfig(def);

	ModuleCache cache;
	const bool ok = pipeline.Build(TheBuilder, def, device, cache);
	ReleaseModules(device, cache);

	if (!ok)
	{
		// build failed, old pipeline untouched and still live
		pipeline.Handle().pipeline = oldPipeline;
		return false;
	}

	// Build succeeded — retire the old one, keep what Build wrote
	if (oldPipeline != VK_NULL_HANDLE)
		m_retiredPipelines.push_back({ oldPipeline, m_currentFrame });

	return true;
}

void PM::Shutdown(VkDevice device)
{
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


bool PM::Pipeline::Build(PipelineBuilder& builder, const PipelineDef& def, VkDevice device, ModuleCache& cache)
{
	ASSERT(def.shaderCount > 0u);
	ASSERT(m_handle.bindPoint == def.bindPoint);

	std::vector<VkPipelineShaderStageCreateInfo> stages;
	stages.reserve(def.shaderCount);

	for (uint32_t s = 0; s < def.shaderCount; ++s)
	{
		const VkShaderModule mod = AcquireModule(def.shaders[s].path, device, cache);
		if (mod == VK_NULL_HANDLE) return false;

		stages.push_back(VkPipelineShaderStageCreateInfo{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = static_cast<VkShaderStageFlagBits>(def.shaders[s].stage),
			.module = mod,
			.pName = "main" });
	}

	m_handle.pipeline = VK_NULL_HANDLE;

	return builder.CreatePipeline(m_handle, stages, device);
}