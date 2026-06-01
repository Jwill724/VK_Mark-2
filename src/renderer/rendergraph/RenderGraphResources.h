#pragma once

#include "../backend/VulkanForward.h"
#include "../RendererDefinitions.h"
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

struct RenderPassExecutionContext
{
	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

	FrameContext*                    frameCtx    = nullptr;
	Profiler*                        profiler    = nullptr;
	BindlessImageTable*              imageTable  = nullptr;
	BindlessBDATable*                bufferTable = nullptr;

	const Scene*                     scene       = nullptr;
	const RD::RenderStateInfo*       frameState  = nullptr;
	const Swapchain*                 swapchain   = nullptr;

	void Reset() { *this = RenderPassExecutionContext{}; }
};

class RenderScope
{
public:
	template<typename T>
	void SetPush(const T& data) noexcept
	{
		static_assert(std::is_trivially_copyable_v<T>);
		m_pushData = const_cast<T*>(&data);
		m_pushSize = sizeof(T);
	}

	void ClearPush() noexcept {
		m_pushData = nullptr;
		m_pushSize = 0u;
	}

	template<typename T, typename Fn>
	void EditPush(Fn&& fn) noexcept
	{
		static_assert(std::is_trivially_copyable_v<T>);

		T* push = static_cast<T*>(
			GetValidatedPushData(
				sizeof(T),
				alignof(T)));

		fn(*push);
	}

	void BindPushConstant(VkCommandBuffer cmd, const PipelineHandle& handle);

	void BindReadImage(
		PushDescriptorWriter& writer,
		uint32_t binding,
		const AllocatedImage& img,
		VkSampler sampler,
		uint32_t miplevel = UINT32_MAX);
	void BindWriteImage(
		PushDescriptorWriter& writer,
		uint32_t binding,
		const AllocatedImage& img,
		uint32_t storageViewIndex = UINT32_MAX);

protected:
	bool m_bSkipPushConstant = false;
	void* m_pushData = nullptr;
	size_t m_pushSize = 0;

	void* GetValidatedPushData(
		size_t expectedSize,
		size_t expectedAlignment) noexcept;
};
