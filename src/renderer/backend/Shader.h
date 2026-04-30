#pragma once

#include <renderer/backend/VulkanTypes.h>
#include <renderer/RendererDefinitions.h>
#include <string>

inline static const std::string baseShaderPath = "res/shaders/";

namespace RD = RendererDefinitions;

inline static const std::string& GetShaderPath(RD::Renderer_Shader id);

class Shader
{
public:
	Shader() = default;
	Shader(RD::Renderer_Shader id, Vulkan_ShaderStage stage)
		: m_id(id)
		, m_stage(static_cast<VkShaderStageFlagBits>(stage))
		, m_path(baseShaderPath + GetShaderPath(id)) {}

	// Returns module + stage info, caller decides when to destroy
	bool CreateModule(VkDevice device, VkShaderModule& outModule) const;

	VkPipelineShaderStageCreateInfo MakeStageInfo(VkShaderModule module) const noexcept
	{
		VkPipelineShaderStageCreateInfo info{};
		info.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		info.stage  = m_stage;
		info.module = module;
		info.pName  = "main";
		return info;
	}

	const std::string& Path() const { return m_path; }
	RD::Renderer_Shader    ID()   const { return m_id;   }

private:
	RD::Renderer_Shader       m_id;
	VkShaderStageFlagBits m_stage;
	std::string           m_path;
};
