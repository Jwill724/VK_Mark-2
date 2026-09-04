#pragma once

#include "../VulkanTypes.h"
#include <array>

inline constexpr uint32_t MAX_PIPELINE_STAGES = 3u;
inline constexpr uint32_t MAX_COLOR_ATTACH = 4u;

using Fmt = Vulkan_Format;

enum class Cmp : uint8_t { Never, Less, LEqual, Greater, GEqual, Always };
enum class CullMode : uint8_t { None, Back, Front };
enum class Mask : uint8_t { R = 1, RG = 3, RGBA = 15 };
enum class BlendMode : uint8_t { Off, Additive, InvSrcColor };

struct BlendDef
{
	BlendMode mode = BlendMode::Off;
	Mask      mask = Mask::RGBA;
};

constexpr BlendDef Add(Mask m = Mask::RGBA) { return { BlendMode::Additive,    m }; }
constexpr BlendDef InvSrc(Mask m = Mask::RGBA) { return { BlendMode::InvSrcColor, m }; }

struct ShaderDef
{
	const char* path = nullptr;
	Vulkan_ShaderStage stage = Vulkan_ShaderStage::COMPUTE_STAGE;
};

struct PipelineDef
{
	std::array<ShaderDef, MAX_PIPELINE_STAGES> shaders{};
	uint32_t shaderCount = 0u;

	VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_MAX_ENUM;

	std::array<Fmt, MAX_COLOR_ATTACH>      color{ Fmt::Undefined, Fmt::Undefined,
												  Fmt::Undefined, Fmt::Undefined };
	std::array<BlendDef, MAX_COLOR_ATTACH> blend{};
	uint32_t colorCount = 0u;

	Fmt                 depth = Fmt::Undefined;
	VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkPolygonMode       polygon = VK_POLYGON_MODE_FILL;
	VkCullModeFlagBits  cull = VK_CULL_MODE_NONE;
	VkFrontFace         frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	VkCompareOp         depthCompare = VK_COMPARE_OP_ALWAYS;
	bool                depthTest = false;
	bool                depthWrite = false;
	float               biasConstant = 0.0f;
	float               biasSlope = 0.0f;

	bool IsDeclared()  const noexcept { return bindPoint != VK_PIPELINE_BIND_POINT_MAX_ENUM; }
	bool IsGraphics()  const noexcept { return bindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS; }
	const char* DebugName() const noexcept
	{
		return shaderCount > 0u ? shaders[shaderCount - 1u].path : "<undeclared>";
	}
};

constexpr VkCompareOp ToVk(Cmp c)
{
	switch (c)
	{
	case Cmp::Never:   return VK_COMPARE_OP_NEVER;
	case Cmp::Less:    return VK_COMPARE_OP_LESS;
	case Cmp::LEqual:  return VK_COMPARE_OP_LESS_OR_EQUAL;
	case Cmp::Greater: return VK_COMPARE_OP_GREATER;
	case Cmp::GEqual:  return VK_COMPARE_OP_GREATER_OR_EQUAL;
	case Cmp::Always:  return VK_COMPARE_OP_ALWAYS;
	}
	return VK_COMPARE_OP_ALWAYS;
}

constexpr VkCullModeFlagBits ToVk(CullMode m)
{
	switch (m)
	{
	case CullMode::Back:  return VK_CULL_MODE_BACK_BIT;
	case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
	case CullMode::None:  return VK_CULL_MODE_NONE;
	}
	return VK_CULL_MODE_NONE;
}

constexpr VkPipelineColorBlendAttachmentState ToVk(BlendDef def)
{
	VkPipelineColorBlendAttachmentState s{};
	s.colorWriteMask = static_cast<VkColorComponentFlags>(def.mask);
	s.blendEnable = (def.mode != BlendMode::Off);
	s.colorBlendOp = VK_BLEND_OP_ADD;
	s.alphaBlendOp = VK_BLEND_OP_ADD;

	switch (def.mode)
	{
	case BlendMode::Additive:
		s.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		s.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		s.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		s.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		break;
	case BlendMode::InvSrcColor:
		s.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
		s.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
		s.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		s.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		break;
	case BlendMode::Off:
		s.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		s.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		s.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		s.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		break;
	}
	return s;
}