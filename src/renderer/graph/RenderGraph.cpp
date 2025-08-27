#include "pch.h"

#include "RenderGraph.h"
#include "renderer/gpu/CommandBuffer.h"
#include "utils/ImageUtils.h"
#include "renderer/backend/Backend.h"

//void RenderGraph::beginFrame(const FrameBindings& bindings) {
//	frameBindings = bindings;
//	imageStates.clear();
//	passes.clear();
//}
//
//void RenderGraph::addPass(RenderGraphPass pass) {
//	passes.push_back(std::move(pass));
//}
//
//VkCommandBufferInheritanceInfo RenderGraph::makeInheritanceInfo(const RenderGraphPass& pass) {
//	VkCommandBufferInheritanceRenderingInfo renderingInherit{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO };
//
//	VkCommandBufferInheritanceInfo inherit { VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO };
//	if (pass.passGoal == PassGoal::Draw && pass.dynamic.enabled) {
//		renderingInherit = {};
//		renderingInherit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO;
//		renderingInherit.colorAttachmentCount = pass.dynamic.renderingInfo.colorAttachmentCount;
//		inherit.pNext = &renderingInherit;
//	}
//
//	return inherit;
//}
//
//void RenderGraph::ensureImageState(
//	VkCommandBuffer cmd,
//	const RenderGraphResource& r)
//{
//	auto& tr = imageStates[r.name];
//
//	const VkImageLayout oldLayout = tr.initialized ? tr.layout : r.layoutBefore;
//	const VkImageLayout newLayout = r.layoutAfter;
//
//	const VkFormat fmt =
//		(r.imageFormat != VK_FORMAT_UNDEFINED) ? r.imageFormat
//		: (tr.format != VK_FORMAT_UNDEFINED ? tr.format
//			: VK_FORMAT_R8G8B8A8_UNORM);
//
//	ImageUtils::transitionImage(
//		cmd, r.image, fmt,
//		oldLayout, newLayout,
//		r.stage,
//		r.accessFlags);
//
//	tr.layout = newLayout;
//	tr.initialized = true;
//	tr.format = fmt;
//}
//
//void RenderGraph::executePassesDeferred(
//	VkCommandBuffer primaryGraphicsCmd,
//	VkCommandPool graphicsSecondaryPool)
//{
//	ASSERT(frameBindings.frame != nullptr);
//
//	auto device = Backend::getDevice();
//	auto& frame = *frameBindings.frame;
//	auto& gpu = *frameBindings.gpu;
//	auto globalPipe = frameBindings.globalLayout;
//	auto unifiedSet = frameBindings.unifiedSet;
//	auto drawExtent = frameBindings.drawExtent;
//
//	// === record secondaries for GRAPHICS passes only ===
//	for (auto& pass : passes) {
//		if (pass.passGoal != PassGoal::Draw || !pass.useSecondary) {
//			continue;
//		}
//
//		// per-pass descriptor tweak if requested
//		if (pass.updateDescriptors) {
//			pass.updateDescriptors(frame.descriptorWriter, unifiedSet, frame);
//			frame.writeFrameDescriptors(device);
//		}
//
//		// Allocate + begin a secondary with inheritance (dynamic rendering will be begun on the primary)
//		VkCommandBufferInheritanceInfo inherit = makeInheritanceInfo(pass);
//
//		VkCommandBuffer secondaryCmd =
//			CommandBuffer::createSecondaryCmd(device, graphicsSecondaryPool, inherit);
//
//		// Record only the draw body here
//		if (pass.executeRich) {
//			RenderPassContext ctx{
//				secondaryCmd,
//				frame,
//				gpu,
//				globalPipe,
//				unifiedSet,
//				frame.set,
//				drawExtent
//			};
//			ctx.bindGlobalSets();
//			pass.executeRich(ctx);
//		}
//		else if (pass.executeCmd) {
//			pass.executeCmd(secondaryCmd);
//		}
//
//		VK_CHECK(vkEndCommandBuffer(secondaryCmd));
//		pass.secondaryCmds.push_back(secondaryCmd);
//
//		// Keep also in the frame for optional later reuse
//		frame.secondaryCmds.push_back(secondaryCmd);
//		frame.secondaryCmdsToFree.push_back(secondaryCmd);
//	}
//
//	// === collect into the primary in order ===
//
//	// 1) pre-pass transitions (images only)—before each pass
//	auto emitPreTransitions = [&](const RenderGraphPass& p) {
//		for (const auto& r : p.reads)  if (r.type == ResourceType::Image) ensureImageState(primaryGraphicsCmd, r);
//		for (const auto& r : p.writes) if (r.type == ResourceType::Image) ensureImageState(primaryGraphicsCmd, r);
//		};
//
//	for (auto& pass : passes) {
//		// For compute/transfer passes we can run them inline on graphics for now.
//		// Later, move them to their own queues and timeline-semaphore sync.
//		if (pass.passGoal == PassGoal::Dispatch) {
//			// descriptor tweak if requested
//			if (pass.updateDescriptors) {
//				pass.updateDescriptors(frame.descriptorWriter, unifiedSet, frame);
//				frame.writeFrameDescriptors(device);
//			}
//
//			emitPreTransitions(pass);
//
//			if (pass.executeRich) {
//				RenderPassContext ctx{
//					primaryGraphicsCmd,
//					frame,
//					gpu,
//					globalPipe,
//					unifiedSet,
//					frame.set,
//					drawExtent
//				};
//				ctx.bindGlobalSets();
//				pass.executeRich(ctx);
//			}
//			else if (pass.executeCmd) {
//				pass.executeCmd(primaryGraphicsCmd);
//			}
//
//			continue;
//		}
//
//		// Graphics pass
//		emitPreTransitions(pass);
//
//		if (pass.dynamic.enabled) {
//			vkCmdBeginRendering(primaryGraphicsCmd, &pass.dynamic.renderingInfo);
//		}
//
//		if (pass.useSecondary && !pass.secondaryCmds.empty()) {
//			vkCmdExecuteCommands(
//				primaryGraphicsCmd,
//				static_cast<uint32_t>(pass.secondaryCmds.size()),
//				pass.secondaryCmds.data());
//		}
//		else {
//			// immediate body on primary
//			if (pass.updateDescriptors) {
//				pass.updateDescriptors(frame.descriptorWriter, unifiedSet, frame);
//				frame.writeFrameDescriptors(device);
//			}
//
//			if (pass.executeRich) {
//				RenderPassContext ctx{
//					primaryGraphicsCmd,
//					frame,
//					gpu,
//					globalPipe,
//					unifiedSet,
//					frame.set,
//					drawExtent
//				};
//				ctx.bindGlobalSets();
//				pass.executeRich(ctx);
//			}
//			else if (pass.executeCmd) {
//				pass.executeCmd(primaryGraphicsCmd);
//			}
//		}
//
//		if (pass.dynamic.enabled) {
//			vkCmdEndRendering(primaryGraphicsCmd);
//		}
//	}
//}