#pragma once

#include "Shader.h"
#include "../RendererDefinitions.h"
#include "PipelineBundles.h"
#include <vector>

namespace RD = RendererDefinitions;

class PipelineBuilder;

class PipelineManager final
{
public:
	// Only need to call these two functions at init
	void CreatePipelineLayout(
		VkDevice device,
		const std::vector<VkDescriptorSetLayout>& descriptorLayouts);
	void InitPipelines(VkDevice device);

	template<typename SlotEnum>
	std::vector<PipelineHandle> GetBundle() // Called for render pass registerations
	{
		constexpr auto& mappings = PipelineBundleTraits<SlotEnum>::mappings;
		std::vector<PipelineHandle> handles;
		handles.reserve(mappings.size());
		for (auto pipelineID : mappings)
			handles.push_back(GetHandle(pipelineID));
		return handles;
	}

	// Called once per frame before submitting work
	// completedFrame = last frame the GPU has fully finished
	void TickFrame(VkDevice device, uint32_t completedFrame, uint32_t framesInFlight = RD::MAX_FRAMES_IN_FLIGHT);

	// Rebuild a single pipeline — safe to call at runtime
	// Old pipeline is retired, not immediately destroyed
	bool Rebuild(RD::Renderer_Pipeline id, VkDevice device);

	// Full shutdown — GPU must be idle before calling this
	void Shutdown(VkDevice device);

	const PipelineLayoutConst& GetGlobalLayout() { return m_globalLayout; }
	const PipelineHandle& GetHandle(RD::Renderer_Pipeline id) { return m_pipelines[static_cast<size_t>(id)].Handle(); }

private:
	class Pipeline
	{
	public:
		Pipeline() = default;
		explicit Pipeline(RD::Renderer_Pipeline id) : m_id(id) {}

		Pipeline& AddShader(RD::Renderer_Shader shader, Vulkan_ShaderStage stage);
		bool Build(PipelineBuilder& builder, PipelinePreset& preset, VkDevice device);

		PipelineHandle& Handle() { return m_handle; }
		RD::Renderer_Pipeline ID() const { return m_id; }

	private:
		RD::Renderer_Pipeline m_id = RD::Renderer_Pipeline::Count;
		std::vector<Shader>   m_shaders;  // {shader stages, shader path}
		PipelineHandle        m_handle{}; // pipeline meta data
	};

	// Core pipeline meta data and building
	std::array<Pipeline, RD::PIPELINE_COUNT> m_pipelines;

	// Pipeline settings and configurations
	std::array<PipelinePreset, RD::PIPELINE_COUNT> m_pipelinePresets;

	// All pipelines shared the same layouts and push constant settings
	PipelineLayoutConst m_globalLayout;

	void SetupPipelineConfig(const PipelinePreset& preset);

	void RegisterPipelines();

	// Retirement tracking — pipelines waiting for GPU to finish with them
	uint32_t m_currentFrame = 0;

	struct RetiredPipeline
	{
		VkPipeline pipeline;
		uint32_t   frameIndex;  // frame it was retired on
	};

	std::vector<RetiredPipeline> m_retiredPipelines;
};
