#pragma once

#include "renderer/frame/FrameContext.h"
#include "renderer/gpu/PipelineManager.h"
#include "engine/platform/profiler/Profiler.h"

namespace RenderPasses {
	struct GraphicsScope {
		PassID passID = PassID::None;
		VkRenderingInfo info{};
		std::vector<VkRenderingAttachmentInfo> colorAttachments;
		VkRenderingAttachmentInfo depthAttachment{};
		bool hasDepth = false;
		uint32_t viewMask{ 0 };
		bool atlasOn = false;
		VkOffset2D atlasOffset{};
		VkExtent2D atlasExtent{};
	};

	struct DispatchIndirectInfo {
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceSize offset = 0;
	};

	struct ComputeScope {
		PassID passID = PassID::None;
		VkExtent2D extent{ 0u, 0u }; // Always set extent to storage output
		VkExtent3D workgroupSize{ 8u, 8u, 1u };
		uint32_t groupCountX = 0u;
		uint32_t groupCountY = 0u;
		uint32_t groupCountZ = 1u;

		void* pushData = nullptr;
		size_t pushSize = 0;

		DispatchIndirectInfo indirect{};

		bool skipGroups = false;
		bool skipPushConstant = false;

		inline void calculateGroups() noexcept {
			if (skipGroups) return;
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

		inline bool isIndirect() const noexcept {
			return indirect.buffer != VK_NULL_HANDLE;
		}

		inline void setIndirect(VkBuffer buffer, VkDeviceSize offset = 0) noexcept {
			indirect.buffer = buffer;
			indirect.offset = offset;
		}

		inline void clearIndirect() noexcept {
			indirect = {};
		}
	};

	void BasePrepass(
		FrameContext& frameCtx,
		GraphicsScope scope,
		Profiler& profiler,
		const bool isTemporalValid);
	void shadowCSMPass(
		FrameContext& frameCtx,
		GraphicsScope scope,
		Profiler& profiler);
	void shadowFlashLightPass(
		FrameContext& frameCtx,
		GraphicsScope scope,
		Profiler& profiler);
	void GTAOPass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler,
		const bool isTemporalValid);
	void hiZGenerationPass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler);
	void volumetricLightingPass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler);
	void screenSpaceContactShadowsPass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler);
	void exposurePass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler,
		const AllocatedBuffer& luminanceBuf,
		const bool transparentVisible,
		const bool hasVisibles,
		const bool isTemporalValid);
	void lensFlarePass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler,
		const bool transparentVisible,
		const bool hasVisibles,
		const bool isTemporalValid);
	void finalCompositePass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler,
		const bool transparentVisible,
		const bool hasVisibles,
		const bool isTemporalValid);
	void chromaticAberrationPass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler,
		const bool hasVisibles);
	void clusteredPass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler);
	void SMAAPass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler);
	void CMAA2Pass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler);
	void FXAAPass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler);
	void TAAPass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler);

	void transparentResolvePass(
		FrameContext& frameCtx,
		ComputeScope scope,
		Profiler& profiler);

	void opaqueMeshPass(
		FrameContext& frameCtx,
		GraphicsScope scope,
		Profiler& profiler);
	void transparentMeshPass(
		FrameContext& frameCtx,
		GraphicsScope scope,
		Profiler& profiler);
	void skyboxPass(
		FrameContext& frameCtx,
		GraphicsScope scope,
		Profiler& profiler,
		const bool hasVisibles);
	void obbLinePass(
		FrameContext& frameCtx,
		GraphicsScope scope,
		Profiler& profiler);

	void dispatchComputePass(
		VkCommandBuffer cmd,
		const PipelineHandle& pipeHandle,
		ComputeScope& scope,
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
		GraphicsScope& scope);

	inline void endRendering(VkCommandBuffer cmd) {
		vkCmdEndRendering(cmd);
	}
}
