#include "pch.h"

#include "RenderGraph.h"

//void RenderGraph::executePasses(VkCommandBuffer cmd, VkCommandPool secondaryPool) {
//	std::unordered_map<std::string, RenderGraphResource> lastSeenAccess;
//
//	for (RenderGraphPass& pass : passes) {
//		std::vector<VkMemoryBarrier2> memoryBarriers;
//
//		for (auto& res : pass.reads) {
//			if (lastSeenAccess.contains(res.name) &&
//				lastSeenAccess[res.name].access != ResourceAccess::Read) {
//				memoryBarriers.push_back(VkMemoryBarrier2{
//					.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
//					.srcStageMask = lastSeenAccess[res.name].stage,
//					.srcAccessMask = lastSeenAccess[res.name].accessFlags,
//					.dstStageMask = res.stage,
//					.dstAccessMask = res.accessFlags
//				});
//			}
//			lastSeenAccess[res.name] = res;
//		}
//
//		for (auto& res : pass.writes) {
//			if (lastSeenAccess.contains(res.name)) {
//				memoryBarriers.push_back(VkMemoryBarrier2{
//					.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
//					.srcStageMask = lastSeenAccess[res.name].stage,
//					.srcAccessMask = lastSeenAccess[res.name].accessFlags,
//					.dstStageMask = res.stage,
//					.dstAccessMask = res.accessFlags
//				});
//			}
//			lastSeenAccess[res.name] = res;
//		}
//
//		if (!memoryBarriers.empty()) {
//			VkDependencyInfo depInfo{
//				.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
//				.memoryBarrierCount = static_cast<uint32_t>(memoryBarriers.size()),
//				.pMemoryBarriers = memoryBarriers.data()
//			};
//			vkCmdPipelineBarrier2(cmd, &depInfo);
//		}
//
//		pass.execute(cmd);
//	}
//}