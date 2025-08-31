#include "pch.h"

#include "RenderPasses.h"
#include "renderer/scene/Visibility.h"
#include "utils/VulkanUtils.h"
#include "utils/BufferUtils.h"
#include "renderer/scene/RenderScene.h"
#include "engine/Engine.h"

static constexpr VkDeviceSize drawCmdSize = sizeof(VkDrawIndexedIndirectCommand);

template<typename PCType>
inline static void bindPushConstants(const PCType& pc, VkCommandBuffer cmd) {
	const PipelineLayoutConst globalLayout = Pipelines::_globalLayout;
	vkCmdPushConstants(
		cmd,
		globalLayout.layout,
		globalLayout.pcRange.stageFlags,
		globalLayout.pcRange.offset,
		static_cast<uint32_t>(sizeof(PCType)),
		&pc);
}

void RenderPasses::depthPrePass(FrameContext& frameCtx, const PipelineHandle& pipeHandle) {
	vkCmdBindPipeline(frameCtx.commandBuffer, pipeHandle.bindPoint, pipeHandle.pipeline);

	bindPushConstants(frameCtx.drawDataPC, frameCtx.commandBuffer);

	vkCmdDrawIndexedIndirect(frameCtx.commandBuffer,
		frameCtx.indirectDrawsBuffer.buffer,
		frameCtx.opaqueRange.first * drawCmdSize,
		frameCtx.opaqueRange.visibleCount,
		drawCmdSize
	);
}

void RenderPasses::skyboxPass(FrameContext& frameCtx, const PipelineHandle& pipeHandle, Profiler& profiler) {
	vkCmdBindPipeline(
		frameCtx.commandBuffer,
		pipeHandle.bindPoint,
		pipeHandle.pipeline);

	const auto& sceneData = RenderScene::getCurrentSceneData();

	glm::mat4 view = glm::mat4(glm::mat3(sceneData.view)); // strip translation

	glm::mat4 proj = sceneData.proj;
	glm::mat4 viewproj = proj * view;

	glm::mat4 invVp = glm::inverse(viewproj);

	bindPushConstants(invVp, frameCtx.commandBuffer);

	vkCmdDraw(frameCtx.commandBuffer, 3, 1, 0, 0);

	// Literally one triangle
	if (profiler.debugToggles.enableStats) {
		profiler.addDirect(1, 1);
	}
}

void RenderPasses::opaqueMeshPass(FrameContext& frameCtx, const PipelineHandle& pipeHandle, Profiler& profiler) {
	vkCmdBindPipeline(frameCtx.commandBuffer, pipeHandle.bindPoint, pipeHandle.pipeline);

	bindPushConstants(frameCtx.drawDataPC, frameCtx.commandBuffer);

	// For ssao, the final blur image
	// Only applied to opaque shading
	frameCtx.descriptorWriter.updatePushSet(
		frameCtx.commandBuffer,
		pipeHandle.bindPoint,
		Pipelines::_globalLayout.layout);

	vkCmdDrawIndexedIndirect(frameCtx.commandBuffer,
		frameCtx.indirectDrawsBuffer.buffer,
		frameCtx.opaqueRange.first * drawCmdSize,
		frameCtx.opaqueRange.visibleCount,
		drawCmdSize
	);

	if (profiler.debugToggles.enableStats) {
		const uint64_t trisOpaque = sumTrianglesIndirectRange(
			frameCtx.indirectDraws,
			frameCtx.opaqueRange.first,
			frameCtx.opaqueRange.visibleCount,
			pipeHandle.topology);

		profiler.addOpaqueIndirect(/*commands*/1,
			/*sub-draws*/frameCtx.opaqueRange.visibleCount,
			/*triangles*/trisOpaque);
	}
}

void RenderPasses::transparentMeshPass(FrameContext& frameCtx, const PipelineHandle& pipeHandle, Profiler& profiler) {
	vkCmdBindPipeline(
		frameCtx.commandBuffer,
		pipeHandle.bindPoint,
		pipeHandle.pipeline);

	bindPushConstants(frameCtx.drawDataPC, frameCtx.commandBuffer);

	vkCmdDrawIndexedIndirect(frameCtx.commandBuffer,
		frameCtx.indirectDrawsBuffer.buffer,
		frameCtx.transparentRange.first * drawCmdSize,
		frameCtx.transparentRange.visibleCount,
		drawCmdSize
	);

	if (profiler.debugToggles.enableStats) {
		const uint64_t trisTransparent = sumTrianglesIndirectRange(
			frameCtx.indirectDraws,
			frameCtx.transparentRange.first,
			frameCtx.transparentRange.visibleCount,
			pipeHandle.topology);

		profiler.addTransparentIndirect(/*commands*/1,
			/*sub-draws*/frameCtx.transparentRange.visibleCount,
			/*triangles*/trisTransparent);
	}
}

void RenderPasses::obbDebugPass(FrameContext& frameCtx, const PipelineHandle& pipeHandle, Profiler& profiler) {
	std::vector<glm::vec3> allVerts;
	std::vector<uint32_t> drawOffsets;

	auto& resources = Engine::getState().getGPUResources();
	const auto& meshes = resources.getResgisteredMeshes().meshData;

	auto emitAABBVerts = [&](const GPUInstance& inst) {
		const auto& aabb = meshes[inst.meshID].localAABB;
		const auto& matrix = RenderScene::_globalTransforms[inst.transformID];
		auto verts = Visibility::GetOBBVertices(aabb, matrix);
		uint32_t offset = static_cast<uint32_t>(allVerts.size());
		drawOffsets.push_back(offset);
		allVerts.insert(allVerts.end(), verts.begin(), verts.end());
	};
	for (const auto& inst : frameCtx.visibleInstances) emitAABBVerts(inst);

	const auto allocator = resources.getAllocator();

	const size_t totalSize = allVerts.size() * sizeof(glm::vec3);

	AllocatedBuffer aabbVBO = BufferUtils::createBuffer(
		totalSize,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_CPU_TO_GPU,
		allocator);
	ASSERT(aabbVBO.info.pMappedData != nullptr);
	memcpy(aabbVBO.mapped, allVerts.data(), totalSize);

	auto aabbBuf = aabbVBO.buffer;
	auto aabbAlloc = aabbVBO.allocation;
	frameCtx.cpuDeletion.push_function([aabbBuf, aabbAlloc, allocator]() mutable {
		BufferUtils::destroyBuffer(aabbBuf, aabbAlloc, allocator);
	});

	vkCmdBindPipeline(
		frameCtx.commandBuffer,
		pipeHandle.bindPoint,
		pipeHandle.pipeline);

	const VkDeviceSize vtxOffset = 0;
	vkCmdBindVertexBuffers(frameCtx.commandBuffer, 0, 1, &aabbVBO.buffer, &vtxOffset);

	struct alignas(16) AABBPush {
		glm::mat4 worldMatrix;
		VkDeviceAddress vertexBuffer;
		uint32_t pad0[2];
	} pc{};
	pc.worldMatrix = RenderScene::getCurrentSceneData().viewproj;
	pc.vertexBuffer = aabbVBO.address;

	bindPushConstants(pc, frameCtx.commandBuffer);

	const uint32_t vertsPerAABB = 24;
	const uint64_t trisPerDraw = trianglesFromNonIndexed(pipeHandle.topology, static_cast<uint64_t>(vertsPerAABB));

	for (uint32_t i = 0; i < drawOffsets.size(); ++i) {
		uint32_t vertexOffset = drawOffsets[i];
		vkCmdDraw(frameCtx.commandBuffer, vertsPerAABB, 1, vertexOffset, 0);
		if (profiler.debugToggles.enableStats) {
			profiler.addDirect(1, trisPerDraw);
		}
	}
}

void RenderPasses::beginRendering(
	VkCommandBuffer cmd,
	const std::vector<AttachmentDesc>& images,
	VkExtent2D extent,
	GraphicsRenderScope& scope)
{
	scope.colorAttachments.clear();
	scope.hasDepth = false;

	for (size_t i = 0; i < images.size(); i++) {
		const auto& desc = images[i];

		VkRenderingAttachmentInfo att = makeAttachmentInfo(desc);

		if (desc.layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL ||
			desc.layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
			scope.depthAttachment = att;
			scope.hasDepth = true;
		}
		else {
			scope.colorAttachments.push_back(att);
		}
	}

	scope.info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	scope.info.renderArea = { { 0, 0 }, extent };
	scope.info.layerCount = 1;
	scope.info.colorAttachmentCount = static_cast<uint32_t>(scope.colorAttachments.size());
	scope.info.pColorAttachments = scope.colorAttachments.data();
	scope.info.pDepthAttachment = scope.hasDepth ? &scope.depthAttachment : nullptr;

	vkCmdBeginRendering(cmd, &scope.info);
	VulkanUtils::defineViewportAndScissor(cmd, extent);
}

// Agnostic for all compute passes
void RenderPasses::dispatchComputePass(
	FrameContext& frameCtx,
	const PipelineHandle& pipeHandle,
	ComputeDispatchScope& scope)
{
	vkCmdBindPipeline(frameCtx.commandBuffer, pipeHandle.bindPoint, pipeHandle.pipeline);

	if (scope.pushData && scope.pushSize > 0) {
		const PipelineLayoutConst globalLayout = Pipelines::_globalLayout;
		vkCmdPushConstants(
			frameCtx.commandBuffer,
			globalLayout.layout,
			globalLayout.pcRange.stageFlags,
			globalLayout.pcRange.offset,
			static_cast<uint32_t>(scope.pushSize),
			scope.pushData);
	}

	auto& writer = frameCtx.descriptorWriter;
	if (writer.enablePushDescriptor) {
		writer.updatePushSet(
			frameCtx.commandBuffer,
			pipeHandle.bindPoint,
			Pipelines::_globalLayout.layout);
	}

	scope.calculateGroups();

	vkCmdDispatch(
		frameCtx.commandBuffer,
		scope.groupCountX,
		scope.groupCountY,
		scope.groupCountZ);
}