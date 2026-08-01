#pragma once

#include "../backend/VulkanForward.h"
#include "../RendererDefinitions.h"
#include "RenderGraphSchedule.h"

namespace RD = RendererDefinitions;

struct RenderResourceUsage
{
	RD::Renderer_RenderTarget target;

	RD::ImageAccess enterAccess;
	RD::ImageAccess exitAccess;

	uint32_t baseMip = 0;
	uint32_t mipCount = 1;

	bool bIsWrite = false;
	bool bManualExitTransition = false;
};

struct PipelineHandle;
struct AllocatedImage;
class PushDescriptorWriter;
class FrameContext;
class Profiler;
class BindlessImageTable;
class BindlessBDATable;
class Scene;
class Swapchain;
class RenderGraph;

struct RenderPassExecutionContext
{
	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

	uint32_t threadSlot = UINT32_MAX;

	FrameContext*                    frameCtx    = nullptr;
	Profiler*                        profiler    = nullptr;
	BindlessImageTable*              imageTable  = nullptr;
	BindlessBDATable*                bufferTable = nullptr;

	const Scene*                     scene       = nullptr;
	const RD::RenderStateInfo*       frameState  = nullptr;
	const Swapchain*                 swapchain   = nullptr;

	PassScheduleInfo* scheduleInfo = nullptr;

	RenderGraph* renderGraph = nullptr;

	void Reset() { *this = RenderPassExecutionContext{}; }
};

class RenderScope
{
public:
	static constexpr size_t PUSH_ALIGNMENT = 16u;

	template<typename T>
	void SetPush(const T& data) noexcept
	{
		static_assert(std::is_trivially_copyable_v<T>);
		static_assert(sizeof(T) <= RD::MAX_PUSH_CONSTANT_SIZE,
			"Push constant exceeds the pipeline layout's declared range.");
		static_assert(alignof(T) <= PUSH_ALIGNMENT,
			"Push struct alignment exceeds the scope's staging buffer alignment.");

		std::memcpy(m_pushData.data(), &data, sizeof(T));
		m_pushSize = sizeof(T);
	}

	void ClearPush() noexcept { m_pushSize = 0u; }

	template<typename T, typename Fn>
	void EditPush(Fn&& fn) noexcept
	{
		static_assert(std::is_trivially_copyable_v<T>);

		T* push = static_cast<T*>(GetValidatedPushData(sizeof(T), alignof(T)));
		if (push) fn(*push);
	}

	void BindPushConstant(VkCommandBuffer cmd, const PipelineHandle& handle);

	void BindReadImage(
		PushDescriptorWriter& writer,
		uint32_t binding,
		const AllocatedImage& img,
		VkSampler sampler,
		uint32_t miplevel = UINT32_MAX,
		RD::ImageAccess declaredAccess = RD::ImageAccess::Read);
	void BindWriteImage(
		PushDescriptorWriter& writer,
		uint32_t binding,
		const AllocatedImage& img,
		uint32_t storageViewIndex = UINT32_MAX,
		RD::ImageAccess declaredAccess = RD::ImageAccess::Write);

protected:
	bool m_bSkipPushConstant = false;
	alignas(PUSH_ALIGNMENT) std::array<std::byte, RD::MAX_PUSH_CONSTANT_SIZE> m_pushData{};
	size_t m_pushSize = 0;

	void* GetValidatedPushData(
		size_t expectedSize,
		size_t expectedAlignment) noexcept;
};
