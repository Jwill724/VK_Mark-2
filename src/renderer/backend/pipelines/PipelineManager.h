#pragma once

#include "PipelineTable.h"
#include <vector>
#include <string_view>
#include <unordered_map>

class PipelineBuilder;

class PipelineManager final
{
public:
	// Only need to call these two functions at init
	void CreatePipelineLayout(
		VkDevice device,
		const std::vector<VkDescriptorSetLayout>& descriptorLayouts);
	void InitPipelines(VkDevice device);

	// Called once per frame before submitting work
	// completedFrame = last frame the GPU has fully finished
	void TickFrame(VkDevice device, uint32_t completedFrame, uint32_t framesInFlight = RD::MAX_FRAMES_IN_FLIGHT);

	// Rebuild a single pipeline — safe to call at runtime
	// Old pipeline is retired, not immediately destroyed
	bool Rebuild(RD::Renderer_Pipeline id, VkDevice device);

	// Full shutdown — GPU must be idle before calling this
	void Shutdown(VkDevice device);

	const PipelineLayoutConst& GetGlobalLayout() const noexcept { return m_globalLayout; }
	const PipelineHandle& GetHandle(RD::Renderer_Pipeline id) const noexcept { return m_pipelines[static_cast<size_t>(id)].Handle(); }

private:
	// Keyed on the table's string literals — lifetime is static, hashing is cheap
	using ModuleCache = std::unordered_map<std::string_view, VkShaderModule>;

	static VkShaderModule AcquireModule(const char* path, VkDevice device, ModuleCache& cache);
	static void           ReleaseModules(VkDevice device, ModuleCache& cache);

	class Pipeline
	{
	public:
		Pipeline() = default;

		void Init(RD::Renderer_Pipeline id) { m_id = id; }
		bool Build(PipelineBuilder& builder, const PipelineDef& def, VkDevice device, ModuleCache& cache);

		PipelineHandle& Handle() { return m_handle; }
		const PipelineHandle& Handle() const { return m_handle; }
		RD::Renderer_Pipeline ID()     const { return m_id; }

	private:
		RD::Renderer_Pipeline m_id = RD::Renderer_Pipeline::Count;
		PipelineHandle        m_handle{}; // pipeline meta data
	};

	// Core pipeline meta data and building
	std::array<Pipeline, RD::PIPELINE_COUNT> m_pipelines;

	// All pipelines shared the same layouts and push constant settings
	PipelineLayoutConst m_globalLayout;

	void SetupPipelineConfig(const PipelineDef& def);

	// Retirement tracking — pipelines waiting for GPU to finish with them
	uint32_t m_currentFrame = 0;

	struct RetiredPipeline
	{
		VkPipeline pipeline;
		uint32_t   frameIndex;  // frame it was retired on
	};

	std::vector<RetiredPipeline> m_retiredPipelines;
};