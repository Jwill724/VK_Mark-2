#pragma once

#include "engine/platform/profiler/ProfilerTypes.h"
#include "renderer/frame/FrameContext.h"

class Profiler {
public:
	class ScopedPass {
	public:
		ScopedPass() = default;

		ScopedPass(
			Profiler& profiler,
			FrameContext& frameCtx,
			VkCommandBuffer cmd,
			PassID passID
		);

		ScopedPass(const ScopedPass&) = delete;
		ScopedPass& operator=(const ScopedPass&) = delete;

		ScopedPass(ScopedPass&& other) noexcept;
		ScopedPass& operator=(ScopedPass&& other) noexcept;

		~ScopedPass();

	private:
		Profiler* _profiler = nullptr;
		FrameContext* _frameCtx = nullptr;
		VkCommandBuffer _cmd = VK_NULL_HANDLE;
		PassID _passID = PassID::None;
		void* _gpuZone = nullptr;
		int64_t _cpuStartTicks = 0;
	};

	Profiler();
	~Profiler();

	void beginFrame();
	void endFrame();

	void startTimer();
	float endTimerMS() const;
	float endTimerSec() const;

	void resetDrawCalls();

	void resetPassStats();
	void markPassActive(PassID passID);

	void addCpuPassTime(
		PassID passID,
		float milliseconds
	);

	void addGpuPassTime(
		PassID passID,
		float milliseconds
	);

	const char* getPassName(PassID passID) const;

	ScopedPass profilePass(
        FrameContext& frameCtx,
		VkCommandBuffer cmd,
		PassID passID
	);


	void initTracyGPU(
		VkPhysicalDevice physicalDevice,
		VkDevice device,
		VkQueue queue,
		VkCommandBuffer cmd
	);

	void shutdownTracyGPU();

	void collectTracyGPU(VkCommandBuffer cmd);

	bool isTracyGPUActive() const;

	bool isTracyCompiledIn() const;
	const std::array<PassTimingStats, static_cast<size_t>(PassID::Count)>& getAllPassStats() const;
	bool isPassActive(PassID passID) const;

	FrameStats& getStats();
	const FrameStats& getStats() const;

	const PassTimingStats& getPassStats(PassID passID) const;
	PassTimingStats& getPassStats(PassID passID);

	void addDirect(
		uint32_t calls,
		uint64_t triangles = 0
	);

	void addOpaqueIndirect(
		uint32_t commands,
		uint32_t subdraws,
		uint64_t triangles = 0
	);

	void addTransparentIndirect(
		uint32_t commands,
		uint32_t subdraws,
		uint64_t triangles = 0
	);

	void enableGPUAccelUsage();
	void disableGPUAccelUsage();
	bool isGPUAccelOn() const;

	VRAMStats getTotalVRAMUsage(
		VkPhysicalDevice physicalDevice,
		VmaAllocator allocator
	);

	void enablePlatformTimerPrecision();
	void disablePlatformTimerPrecision();

	bool rendererWasStalled = false;

	glm::vec3 cameraPos{};
	std::mutex camMutex;

	DebugToggles debugToggles;
	PipelineOverride pipeOverride;

	GTAOPush gtaoSettings;
	GTAOTemporalResolvePush gtaoTempResSettings;
	VolumetricPush volLightSettings;
	LensFlarePush lensFlareSettings;
	SSSPush contactShadowsSettings;

	VkCommandBuffer& getTracyGraphicsCmd() { return tracyGraphicsCmdBuffer; }

private:
	static constexpr size_t PassCount = static_cast<size_t>(PassID::Count);

	void* beginTracyGpuPass(
		VkCommandBuffer cmd,
		PassID passID
	);

	void endTracyGpuPass(void* gpuZone);

	FrameStats _stats{};
	std::array<PassTimingStats, PassCount> _passStats{};

	VkCommandBuffer tracyGraphicsCmdBuffer = VK_NULL_HANDLE;

	int64_t _qpcFrequency = 0;
	int64_t _framePeriodTicks = 0;
	int64_t _nextFrameTick = 0;
	int64_t _startTimerTick = 0;

	double _qpcInverse = 0.0;
	double _frameStartTime = 0.0;
	double _lastFrameTime = 0.0;

	bool _gpuAccelOn = false;
	void* _tracyGpuContext = nullptr;
};
