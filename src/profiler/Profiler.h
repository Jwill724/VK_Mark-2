#pragma once

#include "renderer/backend/VulkanForward.h"
#include "profiler/ProfilerTypes.h"
#include "renderer/RendererDefinitions.h"
#include "renderer/frame/FrameResources.h"

#include <array>
#include <mutex>
#include <string>

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#include <tracy/TracyVulkan.hpp>
#endif

namespace RD = RendererDefinitions;

class FrameContext;

class Profiler
{
public:
	class ScopedPass
	{
	public:
		ScopedPass() = default;

		ScopedPass(
			Profiler&                 profiler,
			FrameContext&             frameCtx,
			VkCommandBuffer           cmd,
			RD::Renderer_Pass trackingID,
			std::string_view          passName);

		ScopedPass(const ScopedPass&)            = delete;
		ScopedPass& operator=(const ScopedPass&) = delete;

		ScopedPass(ScopedPass&&) noexcept;
		ScopedPass& operator=(ScopedPass&&) noexcept;

		~ScopedPass();

	private:
		Profiler*                 m_profiler      = nullptr;
		FrameContext*             m_frameCtx      = nullptr;
		VkCommandBuffer           m_cmd           = VK_NULL_HANDLE;
		RD::Renderer_Pass m_trackingID    = RD::Renderer_Pass::Count;
		void*                     m_gpuZone       = nullptr;
		int64_t                   m_cpuStartTicks = 0;
	};

	Profiler();
	~Profiler();

	void BeginFrame();
	void EndFrame();

	[[nodiscard]] ScopedPass ProfilePass(
		FrameContext&             frameCtx,
		VkCommandBuffer           cmd,
		RD::Renderer_Pass trackingID,
		std::string_view          passName);

	void AddGpuPassTime(RD::Renderer_Pass trackingID, float milliseconds);

	void ResetPassStats();
	void ResetDrawCalls();

	const std::array<PassTimingStats, RD::PASS_COUNT>& GetAllPassStats() const { return m_passStats; }
	const PassTimingStats& GetPassStats(RD::Renderer_Pass trackingID) const;
	PassTimingStats&       GetPassStats(RD::Renderer_Pass trackingID);

	const char* GetPassName(RD::Renderer_Pass trackingID) const
	{
		return m_passStats[static_cast<size_t>(trackingID)].name.c_str();
	}

	bool IsPassActive(RD::Renderer_Pass trackingID) const noexcept
	{
		return m_passStats[static_cast<size_t>(trackingID)].activeThisFrame;
	}

	void InitTracyGPU(VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue, VkCommandBuffer cmd);
	void ShutdownTracyGPU();
	void CollectTracyGPU(VkCommandBuffer cmd);
	bool IsTracyGPUActive()  const noexcept;
	bool IsTracyCompiledIn() const;

	VkCommandBuffer GetTracyGraphicsCmd() const            { return m_tracyGraphicsCmdBuffer; }
	void            SetTracyGraphicsCmd(VkCommandBuffer c) { m_tracyGraphicsCmdBuffer = c; }

	void  StartTimer();
	float EndTimerMS()  const;
	float EndTimerSec() const;

	FrameStats&       getStats();
	const FrameStats& getStats() const;

	void SetGPUName(std::string name)  { m_stats.gpuName = std::move(name); }
	void SetVRAMUsage(VRAMStats stats) { m_stats.vramStats = stats; }

	void AddDirect(uint32_t calls, uint64_t triangles = 0);
	void AddOpaqueIndirect(uint32_t commands, uint32_t subdraws, uint64_t triangles = 0);
	void AddTransparentIndirect(uint32_t commands, uint32_t subdraws, uint64_t triangles = 0);
	void AddCSMIndirect(uint32_t commands, uint32_t subdraws, uint64_t triangles = 0);
	void AddFlashlightIndirect(uint32_t commands, uint32_t subdraws, uint64_t triangles = 0);

	void EnablePlatformTimerPrecision();
	void DisablePlatformTimerPrecision();

	bool rendererWasStalled = false;

	glm::vec3  cameraPos{};
	std::mutex camMutex;
	bool enableWireframeView = false;
	RD::ShadowQuality shadowQuality;
	TotalAssetDataCounts assetCounts;
	RD::RenderToggles   debugToggles;
	SSAOPush            ssaoSettings;
	TAAPush             taaSettings;
	VolumetricPush      volLightSettings;
	LensFlarePush       lensFlareSettings;
	SSSPush             contactShadowsSettings;
	ToneMappingSettings toneMappingSettings;
	LumaExposurePush    lumaExposureSettings;
	ForwardPush         forwardPush;
	SkyboxPush          skyboxPush;
	LightCullingPush    lightCullingPush;
	BindlessAccessPush  smaaTexturesIds;

private:
	void* BeginTracyGpuZone(VkCommandBuffer cmd, RD::Renderer_Pass trackingID);
	void  EndTracyGpuZone(void* zone);

#ifdef TRACY_ENABLE
	struct TracyPassEntry
	{
		std::string name;
		std::unique_ptr<tracy::SourceLocationData> srcLoc;
	};
	std::array<TracyPassEntry, RD::PASS_COUNT> m_tracySourceLocations{};
#endif

	std::array<PassTimingStats, RD::PASS_COUNT> m_passStats{};

	VkCommandBuffer m_tracyGraphicsCmdBuffer = VK_NULL_HANDLE;

	int64_t m_qpcFrequency     = 0;
	int64_t m_framePeriodTicks = 0;
	int64_t m_nextFrameTick    = 0;
	int64_t m_startTimerTick   = 0;

	double m_qpcInverse     = 0.0;
	double m_frameStartTime = 0.0;
	double m_lastFrameTime  = 0.0;

	void* m_tracyGpuContext = nullptr;

	FrameStats m_stats{};
};
