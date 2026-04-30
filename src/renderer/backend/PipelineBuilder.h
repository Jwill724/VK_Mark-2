#pragma once

#include <renderer/backend/VulkanTypes.h>

class PipelineBuilder final
{
public:
	void InitCreateInfoStructs() noexcept
	{
		// clear all of the structs we need back to 0 with their correct stype
		m_inputAssembly = {};
		m_rasterizer    = {};
		m_multisampling = {};
		m_depthStencil  = {};
		m_renderInfo    = {};
		m_colorBlendAttachment = {};
		m_colorBlendAttachments.clear();
	}

	void SetPipelineLayout(VkPipelineLayout layout) noexcept
	{
		m_pipelineLayout = layout;
	}

	void SetFormats(VkFormat color, VkFormat depth) noexcept
	{
		m_colorFormat = color;
		m_depthFormat = depth;
	}

	VkFormat GetColorFormat() const { return m_colorFormat; }
	VkFormat GetDepthFormat() const { return m_depthFormat; }

	PipelineBuilder() { InitCreateInfoStructs(); }

	void InputAssemblyConfig(VkPrimitiveTopology topology) noexcept
	{
		m_inputAssembly.topology               = topology;
		m_inputAssembly.primitiveRestartEnable = VK_FALSE;
	}

	void DepthBiasConfig(uint32_t constantFactor, uint32_t slopeFactor) noexcept
	{
		m_rasterizer.depthBiasEnable         = VK_TRUE;
		m_rasterizer.depthBiasConstantFactor = constantFactor;
		m_rasterizer.depthBiasSlopeFactor    = slopeFactor;
		m_rasterizer.depthBiasClamp          = 0.0f;
	}

	void RasterizerConfig(
		VkPolygonMode   mode,
		VkCullModeFlags cullMode,
		VkFrontFace     frontFace) noexcept
	{
		m_rasterizer.polygonMode = mode;
		m_rasterizer.lineWidth   = 1.0f;
		m_rasterizer.cullMode    = cullMode;
		m_rasterizer.frontFace   = frontFace;
	}

	// No msaa support currently
	void MultisamplingConfig() noexcept
	{
		m_multisampling.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT;

		m_multisampling.sampleShadingEnable   = VK_FALSE;
		m_multisampling.minSampleShading      = 1.0f;
		m_multisampling.pSampleMask           = nullptr;

		m_multisampling.alphaToCoverageEnable = VK_FALSE;
		m_multisampling.alphaToOneEnable      = VK_FALSE;
	}

	void ColorBlendingConfig(
		const std::vector<VkPipelineColorBlendAttachmentState>& colorBlendAttachments,
		VkColorComponentFlags                                   colorComponents,
		bool                                                    blendEnabled,
		VkBlendFactor                                           blendFactor) noexcept
	{
		// When more than one attachment is used, full parameters defined
		if (!colorBlendAttachments.empty())
		{
			m_colorBlendAttachments = colorBlendAttachments;
			return;
		}

		// Default single blend
		m_colorBlendAttachment.colorWriteMask      = colorComponents;
		m_colorBlendAttachment.blendEnable         = blendEnabled;
		m_colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		m_colorBlendAttachment.dstColorBlendFactor = blendFactor;
		m_colorBlendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
		m_colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		m_colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		m_colorBlendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;
	}

	void ColorAndDepthConfig(
		const std::vector<Vulkan_Format>&  colorFormats,
		Vulkan_Format                      depthFormat)
	{
		if (!colorFormats.empty())
		{
			std::vector<VkFormat> vkformats;
			vkformats.reserve(colorFormats.size());
			for (const auto& format : colorFormats)
			{
				vkformats.push_back(static_cast<VkFormat>(format));
			}
			m_renderInfo.colorAttachmentCount    = static_cast<uint32_t>(colorFormats.size());
			m_renderInfo.pColorAttachmentFormats = vkformats.data();
		}
		else
		{
			m_renderInfo.colorAttachmentCount    = 0;
			m_renderInfo.pColorAttachmentFormats = nullptr;
		}

		m_renderInfo.depthAttachmentFormat = static_cast<VkFormat>(depthFormat);
	}

	void DepthStencilConfig(
		bool        depthTestEnabled,
		bool        depthWriteEnabled,
		VkCompareOp depthCompare) noexcept
	{
		m_depthStencil.depthTestEnable       = depthTestEnabled;
		m_depthStencil.depthWriteEnable      = depthWriteEnabled;
		m_depthStencil.depthCompareOp        = depthCompare;
		m_depthStencil.depthBoundsTestEnable = VK_FALSE;
		m_depthStencil.stencilTestEnable     = VK_FALSE;
		m_depthStencil.front                 = {};
		m_depthStencil.back                  = {};
		m_depthStencil.minDepthBounds        = 0.0f;
		m_depthStencil.maxDepthBounds        = 1.0f;
	}

	bool CreatePipeline(
		PipelineHandle&       handle,
		const PipelinePreset& preset,
		VkDevice              device)
	{
		if (preset.shaderStages.empty()) return false;

		if (handle.bindPoint == VK_PIPELINE_BIND_POINT_COMPUTE)
		{
			VkComputePipelineCreateInfo computePipelineCreateInfo {
				.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
				.pNext  = nullptr,
				.stage  = preset.shaderStages.back(),
				.layout = m_pipelineLayout,
			};

			VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &computePipelineCreateInfo, nullptr, &handle.pipeline));

			return true;
		}
		else // Graphics pipeline
		{
			VkPipelineViewportStateCreateInfo viewportState{};
			viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
			viewportState.pNext = nullptr;

			viewportState.viewportCount = 1;
			viewportState.scissorCount  = 1;

			const uint32_t attachmentCount = m_renderInfo.colorAttachmentCount;

			std::vector<VkPipelineColorBlendAttachmentState> blendAttachments;
			if (!m_colorBlendAttachments.empty())
				blendAttachments = m_colorBlendAttachments;
			else
				blendAttachments.resize(attachmentCount, m_colorBlendAttachment);

			VkPipelineColorBlendStateCreateInfo colorBlending{};
			colorBlending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
			colorBlending.pNext           = nullptr;
			colorBlending.logicOpEnable   = VK_FALSE;
			colorBlending.logicOp         = VK_LOGIC_OP_COPY;
			colorBlending.attachmentCount = attachmentCount;
			colorBlending.pAttachments    = attachmentCount > 0 ? blendAttachments.data() : nullptr;

			VkPipelineVertexInputStateCreateInfo vertexInputInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

			VkGraphicsPipelineCreateInfo pipelineInfo{ .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
			pipelineInfo.pNext = &m_renderInfo;

			pipelineInfo.stageCount          = static_cast<uint32_t>(preset.shaderStages.size());
			pipelineInfo.pStages             = preset.shaderStages.data();
			pipelineInfo.pVertexInputState   = &vertexInputInfo;
			pipelineInfo.pInputAssemblyState = &m_inputAssembly;
			pipelineInfo.pViewportState      = &viewportState;
			pipelineInfo.pRasterizationState = &m_rasterizer;
			pipelineInfo.pMultisampleState   = &m_multisampling;
			pipelineInfo.pColorBlendState    = &colorBlending;
			pipelineInfo.pDepthStencilState  = &m_depthStencil;
			pipelineInfo.layout              = m_pipelineLayout;

			VkDynamicState state[]{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

			VkPipelineDynamicStateCreateInfo dynamicInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
			dynamicInfo.pDynamicStates    = &state[0];
			dynamicInfo.dynamicStateCount = 2;

			pipelineInfo.pDynamicState    = &dynamicInfo;

			VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &handle.pipeline));

			return true;
		}

		return false;
	}

private:
	VkPipelineLayout                                 m_pipelineLayout = VK_NULL_HANDLE;

	VkPipelineInputAssemblyStateCreateInfo           m_inputAssembly;
	VkPipelineRasterizationStateCreateInfo           m_rasterizer;
	VkPipelineColorBlendAttachmentState              m_colorBlendAttachment;
	std::vector<VkPipelineColorBlendAttachmentState> m_colorBlendAttachments;
	VkPipelineMultisampleStateCreateInfo             m_multisampling;
	VkPipelineDepthStencilStateCreateInfo            m_depthStencil;
	VkPipelineRenderingCreateInfo                    m_renderInfo;

	VkFormat                                         m_colorFormat     = VK_FORMAT_UNDEFINED;
	VkFormat                                         m_depthFormat     = VK_FORMAT_UNDEFINED;

};
