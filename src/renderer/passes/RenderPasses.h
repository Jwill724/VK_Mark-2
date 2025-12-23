#pragma once

#include "renderer/frame/FrameContext.h"
#include "renderer/gpu/PipelineManager.h"
#include "engine/platform/profiler/Profiler.h"

namespace RenderPasses {
	struct GraphicsRenderScope {
		VkRenderingInfo info{};
		std::vector<VkRenderingAttachmentInfo> colorAttachments;
		VkRenderingAttachmentInfo depthAttachment{};
		bool hasDepth = false;
		uint32_t viewMask{ 0 };
	};

	struct ComputeDispatchScope {
		VkExtent2D extent{ 0u, 0u }; // Always set extent to storage output
		VkExtent3D workgroupSize{ 8u, 8u, 1u };
		uint32_t groupCountX = 0u;
		uint32_t groupCountY = 0u;
		uint32_t groupCountZ = 1u;

		void* pushData = nullptr;
		size_t pushSize = 0;

		inline void calculateGroups() noexcept {
			groupCountX = (extent.width + workgroupSize.width - 1u) / workgroupSize.width;
			groupCountY = (extent.height + workgroupSize.height - 1u) / workgroupSize.height;
			groupCountZ = workgroupSize.depth; // usually 1
		}

		template<typename T>
		inline void setPush(T& data) noexcept
		{
			static_assert(std::is_trivially_copyable_v<T>);
			pushData = &data;
			pushSize = sizeof(T);
		}

		inline void clearPush() noexcept {
			pushData = nullptr;
			pushSize = 0u;
		}

		template <typename T, typename Fn>
		inline void editPush(Fn&& fn) noexcept {
			ASSERT(pushData != nullptr);
			ASSERT(pushSize == sizeof(T));
			ASSERT((reinterpret_cast<uintptr_t>(pushData) % alignof(T)) == 0);

			T* push = static_cast<T*>(pushData);
			fn(*push);
		}
	};

	void depthPrePass(FrameContext& frameCtx,
		const PipelineHandle& pipeHandle,
		const bool isTemporalValid);
	void shadowCSMPass(FrameContext& frameCtx,
		const PipelineHandle& pipeHandle);
	void SSAOPass(FrameContext& frameCtx,
		ComputeDispatchScope ssaoScope);
	void GTAOPass(FrameContext& frameCtx,
		ComputeDispatchScope gtaoScope,
		const bool isTemporalValid);
	void depthPyramidPass(FrameContext& frameCtx);
	void volumetricLightingPass(FrameContext& frameCtx,
		ComputeDispatchScope volLightScope);
	void exposurePass(FrameContext& frameCtx,
		ComputeDispatchScope exposureScope,
		const AllocatedBuffer& luminanceBuf,
		const bool transparentVisible);
	void lensFlarePass(FrameContext& frameCtx,
		ComputeDispatchScope lensFlareScope,
		const bool transparentVisible);
	void toneMapPass(FrameContext& frameCtx,
		ComputeDispatchScope toneMapScope,
		const bool transparentVisible,
		const bool hasVisibles,
		const DebugToggles& debug);

	void opaqueMeshPass(FrameContext& frameCtx,
		const PipelineHandle& pipeHandle,
		Profiler& profiler);
	void transparentMeshPass(FrameContext& frameCtx,
		const PipelineHandle& pipeHandle,
		Profiler& profiler);
	void skyboxPass(FrameContext& frameCtx,
		const PipelineHandle& pipeHandle,
		Profiler& profiler);
	void obbLinePass(
		FrameContext& frameCtx,
		const PipelineHandle& pipeHandle,
		Profiler& profiler);
	void CascadeVPLinePass(
		FrameContext& frameCtx,
		const PipelineHandle& pipeHandle,
		Profiler& profiler);

	void dispatchComputePass(
		VkCommandBuffer cmd,
		const PipelineHandle& pipeHandle,
		ComputeDispatchScope& scope,
		DescriptorWriter& writer);

	inline VkRenderingAttachmentInfo makeAttachmentInfo(const AttachmentDesc& desc) {
		VkRenderingAttachmentInfo info{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
		info.imageView = desc.imageView;
		info.imageLayout = desc.layout;
		info.resolveMode = desc.resolveMode;
		info.resolveImageView = desc.resolveView;
		info.resolveImageLayout = desc.resolveLayout;
		info.loadOp = desc.loadOp;
		info.storeOp = desc.storeOp;
		info.clearValue = desc.clearValue;
		return info;
	}

	void beginRendering(
		VkCommandBuffer cmd,
		const std::vector<AttachmentDesc>& images,
		VkExtent2D extent,
		GraphicsRenderScope& scope);

	inline void endRendering(VkCommandBuffer cmd) {
		vkCmdEndRendering(cmd);
	}
}