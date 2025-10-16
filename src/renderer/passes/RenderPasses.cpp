#include "pch.h"

#include "RenderPasses.h"
#include "renderer/scene/Visibility.h"
#include "utils/VulkanUtils.h"
#include "utils/BufferUtils.h"
#include "utils/ImageUtils.h"
#include "renderer/scene/RenderScene.h"
#include "engine/Engine.h"

static constexpr VkDeviceSize drawCmdSize = sizeof(VkDrawIndexedIndirectCommand);
static constexpr uint32_t vertsLineCount = 24u;

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

void RenderPasses::shadowCSMPass(FrameContext& frameCtx, const PipelineHandle& pipeHandle) {
	const auto& shadowImg = ResourceManager::getShadowMapImage();

	ImageUtils::transitionImage(
		frameCtx.commandBuffer,
		shadowImg.image,
		shadowImg.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	AttachmentDesc shadowDepth{};
	shadowDepth.imageView = shadowImg.imageView;
	shadowDepth.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	shadowDepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	shadowDepth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	shadowDepth.clearValue.depthStencil.depth = 1.0f;

	RenderPasses::GraphicsRenderScope csmScope;
	csmScope.info.layerCount = MAX_CASCADES; // Pipeline is hard defined with this
	csmScope.info.viewMask = (1u << MAX_CASCADES) - 1u;
	RenderPasses::beginRendering(
		frameCtx.commandBuffer,
		{ shadowDepth },
		{ shadowImg.imageExtent.width, shadowImg.imageExtent.height },
		csmScope);

	vkCmdBindPipeline(frameCtx.commandBuffer,
		pipeHandle.bindPoint,
		pipeHandle.pipeline);

	vkCmdDrawIndexedIndirect(frameCtx.commandBuffer,
		frameCtx.indirectDrawsBuffer.buffer,
		frameCtx.opaqueRange.first * drawCmdSize,
		frameCtx.opaqueRange.visibleCount,
		drawCmdSize);

	RenderPasses::endRendering(frameCtx.commandBuffer);

	ImageUtils::transitionImage(
		frameCtx.commandBuffer,
		shadowImg.image,
		shadowImg.imageFormat,
		VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
}

void RenderPasses::depthPrePass(FrameContext& frameCtx, const PipelineHandle& pipeHandle) {
	auto& depthResolved = ResourceManager::getDepthResolvedImage();
	auto& normal = ResourceManager::getNormalImage();

	ImageUtils::transitionImage(
		frameCtx.commandBuffer,
		depthResolved.image,
		depthResolved.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	ImageUtils::transitionImage(
		frameCtx.commandBuffer,
		normal.image,
		normal.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	AttachmentDesc prepassDepth{};
	prepassDepth.imageView = depthResolved.imageView;
	prepassDepth.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	prepassDepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	prepassDepth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	prepassDepth.clearValue.depthStencil.depth = 1.0f;

	AttachmentDesc prepassNormal{};
	prepassNormal.imageView = normal.imageView;
	prepassNormal.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	prepassNormal.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	prepassNormal.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	prepassNormal.clearValue.color = { { 0.5f, 0.5f, 1.0f, 1.0f } };

	RenderPasses::GraphicsRenderScope depthScope;
	RenderPasses::beginRendering(
		frameCtx.commandBuffer,
		{ prepassNormal, prepassDepth },
		{ depthResolved.imageExtent.width, depthResolved.imageExtent.height },
		depthScope);

	vkCmdBindPipeline(frameCtx.commandBuffer, pipeHandle.bindPoint, pipeHandle.pipeline);

	vkCmdDrawIndexedIndirect(frameCtx.commandBuffer,
		frameCtx.indirectDrawsBuffer.buffer,
		frameCtx.opaqueRange.first * drawCmdSize,
		frameCtx.opaqueRange.visibleCount,
		drawCmdSize
	);

	RenderPasses::endRendering(frameCtx.commandBuffer);

	// Transition images to be sampled
	ImageUtils::transitionImage(
		frameCtx.commandBuffer,
		depthResolved.image,
		depthResolved.imageFormat,
		VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);

	ImageUtils::transitionImage(
		frameCtx.commandBuffer,
		normal.image,
		normal.imageFormat,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void RenderPasses::SSAOPass(FrameContext& frameCtx, ComputeDispatchScope ssaoScope) {
	auto& depthResolved = ResourceManager::getDepthResolvedImage();
	auto& normal = ResourceManager::getNormalImage();
	auto& ssaoImg = ResourceManager::getSSAOImage();
	auto& noiseTex = ResourceManager::getSSAONoiseImage();
	auto& ssaoBlurH = ResourceManager::getSSAOBlurHImage();
	auto& ssaoBlurV = ResourceManager::getSSAOBlurVImage();

	auto depthSampler = ResourceManager::getDepthSampler();
	auto normalSampler = ResourceManager::getNormalSampler();
	auto noiseSampler = ResourceManager::getNoiseSampler();
	auto ssaoSampler = ResourceManager::getSSAOSampler();

	// Transition SSAO output to storage writable
	ImageUtils::transitionImage(
		frameCtx.commandBuffer,
		ssaoImg.image,
		ssaoImg.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_GENERAL);

	// Push writing for main ssao pass

	// depth
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_DEPTH_TEX,
		depthResolved.imageView,
		depthSampler,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
	);

	// normal
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_NORMAL_TEX,
		normal.imageView,
		normalSampler
	);

	// noise texture
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_NOISE_TEX,
		noiseTex.imageView,
		noiseSampler
	);

	// SSAO output
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_TEX,
		ssaoImg.imageView
	);

	// =================
	// === MAIN SSAO ===
	RenderPasses::dispatchComputePass(
		frameCtx,
		Pipelines::getHandle(PipelineID::SSAO),
		ssaoScope);


	// ============================
	// === SSAO BLUR HORIZONTAL ===

	// Transition SSAO to input
	ImageUtils::transitionImage(
		frameCtx.commandBuffer,
		ssaoImg.image,
		ssaoImg.imageFormat,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// Transition blur h for output
	ImageUtils::transitionImage(
		frameCtx.commandBuffer,
		ssaoBlurH.image,
		ssaoBlurH.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_GENERAL);


	// Push writing for blur horizontal
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_TEX,
		ssaoImg.imageView,
		ssaoSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_TEX,
		ssaoBlurH.imageView
	);

	RenderPasses::dispatchComputePass(
		frameCtx,
		Pipelines::getHandle(PipelineID::SSAOBlurH),
		ssaoScope);

	// ==========================
	// === SSAO BLUR VERTICAL ===

	// Transition blur h for input
	ImageUtils::transitionImage(
		frameCtx.commandBuffer,
		ssaoBlurH.image,
		ssaoBlurH.imageFormat,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// Transition blur v for outout
	ImageUtils::transitionImage(
		frameCtx.commandBuffer,
		ssaoBlurV.image,
		ssaoBlurV.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_GENERAL);

	// Push writing for blur vertical
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_TEX,
		ssaoBlurH.imageView,
		ssaoSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_TEX,
		ssaoBlurV.imageView
	);

	RenderPasses::dispatchComputePass(
		frameCtx,
		Pipelines::getHandle(PipelineID::SSAOBlurV),
		ssaoScope);

	// Final transition before lighting
	// ssaoBlurV final output
	ImageUtils::transitionImage(
		frameCtx.commandBuffer,
		ssaoBlurV.image,
		ssaoBlurV.imageFormat,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);
}

void RenderPasses::skyboxPass(FrameContext& frameCtx, const PipelineHandle& pipeHandle, Profiler& profiler) {
	vkCmdBindPipeline(
		frameCtx.commandBuffer,
		pipeHandle.bindPoint,
		pipeHandle.pipeline);

	const auto& sceneData = RenderScene::getCurrentSceneData();

	glm::mat4 view = glm::mat4(glm::mat3(sceneData.view)); // strip translation

	glm::mat4 viewproj = sceneData.proj * view;

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

void RenderPasses::obbLinePass(
	FrameContext& frameCtx,
	const PipelineHandle& pipeHandle,
	Profiler& profiler)
{
	std::vector<glm::vec3> allVerts;
	std::vector<uint32_t> drawOffsets;

	auto& resources = Engine::getState().getGPUResources();
	const auto& meshes = resources.getResgisteredMeshes().meshData;

	auto emitOBBVerts = [&](const GPUInstance& inst) {
		const auto& aabb = meshes[inst.meshID].localAABB;
		const auto& matrix = RenderScene::_globalTransforms[inst.transformID];
		auto verts = Visibility::GetOBBVertices(aabb, matrix);
		uint32_t offset = static_cast<uint32_t>(allVerts.size());
		drawOffsets.push_back(offset);
		allVerts.insert(allVerts.end(), verts.begin(), verts.end());
	};
	for (const auto& inst : frameCtx.visibleInstances) emitOBBVerts(inst);

	const auto allocator = resources.getAllocator();

	const size_t totalSize = allVerts.size() * sizeof(glm::vec3);

	AllocatedBuffer obbVBO = BufferUtils::createBuffer(
		totalSize,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_CPU_TO_GPU,
		allocator);
	ASSERT(obbVBO.info.pMappedData != nullptr);
	memcpy(obbVBO.mapped, allVerts.data(), totalSize);

	auto aabbBuf = obbVBO.buffer;
	auto aabbAlloc = obbVBO.allocation;
	frameCtx.cpuDeletion.push_function([aabbBuf, aabbAlloc, allocator]() mutable {
		BufferUtils::destroyBuffer(aabbBuf, aabbAlloc, allocator);
	});

	vkCmdBindPipeline(
		frameCtx.commandBuffer,
		pipeHandle.bindPoint,
		pipeHandle.pipeline);

	const VkDeviceSize vtxOffset = 0;
	vkCmdBindVertexBuffers(frameCtx.commandBuffer, 0, 1, &obbVBO.buffer, &vtxOffset);

	static struct alignas(16) OBBPush {
		VkDeviceAddress vertexBuffer;
		uint32_t pad0[2];
	} pc{};
	pc.vertexBuffer = obbVBO.address;

	bindPushConstants(pc, frameCtx.commandBuffer);

	for (uint32_t i = 0; i < drawOffsets.size(); ++i) {
		uint32_t vertexOffset = drawOffsets[i];
		vkCmdDraw(frameCtx.commandBuffer, vertsLineCount, 1, vertexOffset, 0);
		if (profiler.debugToggles.enableStats) {
			profiler.addDirect(1);
		}
	}
}

void RenderPasses::CascadeVPLinePass(
	FrameContext& frameCtx,
	const PipelineHandle& pipeHandle,
	Profiler& profiler)
{
	static const std::array<glm::vec3, 8> ndcCorners {
		glm::vec3(-1, -1, 0),
		glm::vec3( 1, -1, 0),
		glm::vec3(-1,  1, 0),
		glm::vec3( 1,  1, 0),
		glm::vec3(-1, -1, 1),
		glm::vec3( 1, -1, 1),
		glm::vec3(-1,  1, 1),
		glm::vec3( 1,  1, 1)
	};

	static const uint32_t edgeIndices[24] {
		0,1, 1,3, 3,2, 2,0, // near plane
		4,5, 5,7, 7,6, 6,4, // far plane
		0,4, 1,5, 2,6, 3,7  // verticals
	};

	const auto allocator = Engine::getState().getGPUResources().getAllocator();
	const auto& cascadeCSM = RenderScene::getShadowCSM();

	// Turn from normalized to world view
	std::array<glm::vec3, MAX_CASCADES * 8> worldCorners{};
	std::array<glm::vec3, MAX_CASCADES * 24> lineVerts{};

	for (uint32_t i = 0; i < MAX_CASCADES; ++i) {
		glm::mat4 invVP = glm::inverse(cascadeCSM.cascadeVP[i]);

		uint32_t base = i * 8;
		for (uint32_t c = 0; c < 8; ++c) {
			glm::vec4 corner = invVP * glm::vec4(ndcCorners[c], 1.0f);
			worldCorners[static_cast<size_t>(base + c)] = glm::vec3(corner) / corner.w; // perspective divide
		}

		uint32_t lineBase = i * 24;
		for (uint32_t e = 0; e < 24; ++e) {
			lineVerts[static_cast<size_t>(lineBase + e)] = worldCorners[static_cast<size_t>(base) + edgeIndices[e]];
		}
	}

	const size_t totalSize = lineVerts.size() * sizeof(glm::vec3);

	AllocatedBuffer cascadeVPVBO = BufferUtils::createBuffer(
		totalSize,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_CPU_TO_GPU,
		allocator);
	ASSERT(cascadeVPVBO.info.pMappedData != nullptr);
	memcpy(cascadeVPVBO.mapped, lineVerts.data(), totalSize);

	auto cascadeVPBuf = cascadeVPVBO.buffer;
	auto cascadeVPAlloc = cascadeVPVBO.allocation;
	frameCtx.cpuDeletion.push_function([cascadeVPBuf, cascadeVPAlloc, allocator]() mutable {
		BufferUtils::destroyBuffer(cascadeVPBuf, cascadeVPAlloc, allocator);
	});

	vkCmdBindPipeline(
		frameCtx.commandBuffer,
		pipeHandle.bindPoint,
		pipeHandle.pipeline);

	const VkDeviceSize vtxOffset = 0;
	vkCmdBindVertexBuffers(frameCtx.commandBuffer, 0, 1, &cascadeVPVBO.buffer, &vtxOffset);

	static struct alignas(16) CascadeVPPush {
		VkDeviceAddress cascadeVPVertBuffer;
		uint32_t pad0[2];
	} pc{};
	pc.cascadeVPVertBuffer = cascadeVPVBO.address;

	bindPushConstants(pc, frameCtx.commandBuffer);

	vkCmdDraw(frameCtx.commandBuffer, static_cast<uint32_t>(lineVerts.size()), 1, 0, 0);

	if (profiler.debugToggles.enableStats) {
		profiler.addDirect(1);
	}
}

void RenderPasses::ToneMapPass(
	FrameContext& frameCtx,
	ComputeDispatchScope toneScope,
	AllocatedImage& toneMap)
{
	auto& draw = ResourceManager::getDrawImage();
	auto linearSampler = ResourceManager::getDefaultSamplerLinear();

	ImageUtils::transitionImage(
		frameCtx.commandBuffer,
		draw.image,
		draw.imageFormat,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	ImageUtils::transitionImage(
		frameCtx.commandBuffer,
		toneMap.image,
		toneMap.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_GENERAL);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_TEX,
		draw.imageView,
		linearSampler);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_TEX,
		toneMap.imageView);

	RenderPasses::dispatchComputePass(
		frameCtx,
		Pipelines::getHandle(PipelineID::ToneMap),
		toneScope);

	// Transition for swapchain
	ImageUtils::transitionImage(
		frameCtx.commandBuffer,
		toneMap.image,
		toneMap.imageFormat,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
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