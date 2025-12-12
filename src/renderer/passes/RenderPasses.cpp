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
		frameCtx.cmdBuffer,
		shadowImg.image,
		shadowImg.format,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	AttachmentDesc shadowDepth{};
	shadowDepth.imageView = shadowImg.imageView;
	shadowDepth.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	shadowDepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	shadowDepth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	shadowDepth.clearValue.depthStencil.depth = 1.0f;

	RenderPasses::GraphicsRenderScope csmScope;
	csmScope.info.layerCount = MAX_SHADOW_CASCADES; // Pipeline is hard defined with this
	csmScope.info.viewMask = (1u << MAX_SHADOW_CASCADES) - 1u;
	RenderPasses::beginRendering(
		frameCtx.cmdBuffer,
		{ shadowDepth },
		{ shadowImg.extent.width, shadowImg.extent.height },
		csmScope);

	vkCmdBindPipeline(frameCtx.cmdBuffer,
		pipeHandle.bindPoint,
		pipeHandle.pipeline);

	vkCmdDrawIndexedIndirect(frameCtx.cmdBuffer,
		frameCtx.indirectDrawsBuffer.buffer,
		frameCtx.opaqueRange.first * drawCmdSize,
		frameCtx.opaqueRange.visibleCount,
		drawCmdSize);

	RenderPasses::endRendering(frameCtx.cmdBuffer);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		shadowImg.image,
		shadowImg.format,
		VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
}

void RenderPasses::depthPrePass(FrameContext& frameCtx, const PipelineHandle& pipeHandle) {
	auto& depthResolved = ResourceManager::getDepthResolvedImage();
	auto& normal = ResourceManager::getNormalImage();

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		depthResolved.image,
		depthResolved.format,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		normal.image,
		normal.format,
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
		frameCtx.cmdBuffer,
		{ prepassNormal, prepassDepth },
		{ depthResolved.extent.width, depthResolved.extent.height },
		depthScope);

	vkCmdBindPipeline(frameCtx.cmdBuffer, pipeHandle.bindPoint, pipeHandle.pipeline);

	vkCmdDrawIndexedIndirect(frameCtx.cmdBuffer,
		frameCtx.indirectDrawsBuffer.buffer,
		frameCtx.opaqueRange.first * drawCmdSize,
		frameCtx.opaqueRange.visibleCount,
		drawCmdSize
	);

	RenderPasses::endRendering(frameCtx.cmdBuffer);

	// Transition images to be sampled
	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		depthResolved.image,
		depthResolved.format,
		VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		normal.image,
		normal.format,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void RenderPasses::depthPyramidPass(FrameContext& frameCtx) {
	auto& depthResolved = ResourceManager::getDepthResolvedImage();
	auto nearestClampSampler = ResourceManager::getNearestClampSampler();
	auto& depthPyramid = ResourceManager::getDepthPyramidImage();
	auto depthPyramidSampler = ResourceManager::getDepthPyramidSampler();

	// Transition all mips to GENERAL for compute writes
	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		depthPyramid.image,
		depthPyramid.format,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_GENERAL,
		0,                         // Start at base mip
		depthPyramid.mipLevelCount // All levels transitioned
	);

	// Uses default workgroup size 8x8x1
	ComputeDispatchScope depthPyramidScope;

	struct alignas(16) DepthPyramidPush {
		uint32_t mipLevel;
		float pad0;
		glm::vec2 invSize; // 1.0 / output mip res
	} push{};

	depthPyramidScope.setPush(&push);

	VkExtent3D srcExtent = depthPyramid.extent; // Start at full res
	VkExtent3D dstExtent = depthPyramid.extent; // updated after each iteration

	for (uint32_t mip = 0; mip < depthPyramid.mipLevelCount; ++mip) {
		VkImageView srcView = (mip == 0)
			? depthResolved.imageView
			: depthPyramid.storageViews[static_cast<size_t>(mip - 1)];

		VkSampler srcSampler = (mip == 0)
			? nearestClampSampler
			: depthPyramidSampler;

		VkImageLayout srcLayout = (mip == 0)
			? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
			: VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		// Write to this mip
		VkImageView dstView = depthPyramid.storageViews[mip];

		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_DEPTH_TEX,
			srcView,
			srcSampler,
			srcLayout);

		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_OUTPUT_1_TEX,
			dstView);

		push.mipLevel = mip;
		push.invSize = {
			1.0f / static_cast<float>(srcExtent.width),
			1.0f / static_cast<float>(srcExtent.height)
		};

		depthPyramidScope.extent = { dstExtent.width, dstExtent.height };

		dispatchComputePass(
			frameCtx.cmdBuffer,
			Pipelines::getHandle(PipelineID::DepthPyramid),
			depthPyramidScope,
			frameCtx.descriptorWriter);

		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			depthPyramid.image,
			depthPyramid.format,
			VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			mip, // Current mip transition
			1    // How many mips to transition in this case one
		);

		srcExtent = dstExtent;

		// Next mip size
		dstExtent.width = std::max(1u, dstExtent.width >> 1);
		dstExtent.height = std::max(1u, dstExtent.height >> 1);
	}
}

void RenderPasses::GTAOPass(
	FrameContext& frameCtx,
	ComputeDispatchScope gtaoScope,
	Profiler& profiler) {
	auto& depthPyramid = ResourceManager::getDepthPyramidImage();
	auto& normal = ResourceManager::getNormalImage();
	auto& rawAO = ResourceManager::getAORawImage();
	auto& noise = ResourceManager::get4x4NoiseImage();
	auto& aoTemp = ResourceManager::getAOTempImage();
	auto& bentNormal = ResourceManager::getBentNormalImage();
	auto& edgeInfo = ResourceManager::getEdgeInfoImage();

	auto nearestClampSampler = ResourceManager::getNearestClampSampler();
	auto depthPyramidSampler = ResourceManager::getDepthPyramidSampler();
	auto noiseSampler = ResourceManager::getNoiseSampler();
	auto aoSampler = ResourceManager::getAOSampler();
	auto nearSampler = ResourceManager::getDefaultSamplerNearest();

	auto& gtaoPush = profiler.gtaoSettings;

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		rawAO.image,
		rawAO.format,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_GENERAL);
	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		bentNormal.image,
		bentNormal.format,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_GENERAL);
	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		edgeInfo.image,
		edgeInfo.format,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_GENERAL);

	// =================
	// === GTAO MAIN ===

	// Inputs
	// Depth pyramid
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_DEPTH_TEX,
		depthPyramid.imageView,
		depthPyramidSampler);
	// Normals
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_NORMAL_TEX,
		normal.imageView,
		nearestClampSampler);
	// 4x4 noise
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_NOISE_TEX,
		noise.imageView,
		noiseSampler);

	// Outputs
	// raw ao
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_1_TEX,
		rawAO.imageView);
	// bent normals
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_2_TEX,
		bentNormal.imageView);
	// edge info
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_3_TEX,
		edgeInfo.imageView);

	RenderPasses::dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::GTAO),
		gtaoScope,
		frameCtx.descriptorWriter);

	// ==============================
	// === GTAO FILTER HORIZONTAL ===

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		rawAO.image,
		rawAO.format,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// Transition edge info once for both filter passes
	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		edgeInfo.image,
		edgeInfo.format,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		aoTemp.image,
		aoTemp.format,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_GENERAL);

	// Inputs
	// ao
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_1_TEX,
		rawAO.imageView,
		aoSampler
	);
	// edge info
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_2_TEX,
		edgeInfo.imageView,
		nearSampler
	);

	// Output
	// Filtered horizontal
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_1_TEX,
		aoTemp.imageView
	);

	gtaoPush.blurDirection = { 1.0f, 0.0f }; // Horizontal
	RenderPasses::dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::GTAOFilter),
		gtaoScope,
		frameCtx.descriptorWriter);

	// ============================
	// === GTAO FILTER VERTICAL ===

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		aoTemp.image,
		aoTemp.format,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		rawAO.image,
		rawAO.format,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_IMAGE_LAYOUT_GENERAL);


	// Inputs
	// Filtered Horionzal ao
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_1_TEX,
		aoTemp.imageView,
		aoSampler
	);
	// edge info
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_2_TEX,
		edgeInfo.imageView,
		nearSampler
	);

	// Output
	// final filtered ao
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_1_TEX,
		rawAO.imageView
	);

	gtaoPush.blurDirection = { 0.0f, 1.0f }; // Vertical
	RenderPasses::dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::GTAOFilter),
		gtaoScope,
		frameCtx.descriptorWriter);

	// Both images required for shading
	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		rawAO.image,
		rawAO.format,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);
	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		bentNormal.image,
		bentNormal.format,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void RenderPasses::SSAOPass(FrameContext& frameCtx, ComputeDispatchScope ssaoScope, Profiler& profiler) {
	auto& depthResolved = ResourceManager::getDepthResolvedImage();
	auto& normal = ResourceManager::getNormalImage();
	auto& ssaoImg = ResourceManager::getAORawImage();
	auto& noiseTex = ResourceManager::get4x4NoiseImage();
	auto& ssaoBlur = ResourceManager::getAOTempImage();

	auto nearestClampSampler = ResourceManager::getNearestClampSampler();
	auto noiseSampler = ResourceManager::getNoiseSampler();
	auto ssaoSampler = ResourceManager::getAOSampler();

	auto& ssaoPush = profiler.ssaoSettings;

	// Transition SSAO output to storage writable
	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		ssaoImg.image,
		ssaoImg.format,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_GENERAL);

	// Push writing for main ssao pass

	// depth
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_DEPTH_TEX,
		depthResolved.imageView,
		nearestClampSampler,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
	);

	// normal
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_NORMAL_TEX,
		normal.imageView,
		nearestClampSampler
	);

	// noise texture
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_NOISE_TEX,
		noiseTex.imageView,
		noiseSampler
	);

	// SSAO output
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_1_TEX,
		ssaoImg.imageView
	);

	// =================
	// === MAIN SSAO ===
	RenderPasses::dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::SSAO),
		ssaoScope,
		frameCtx.descriptorWriter);


	// ============================
	// === SSAO BLUR HORIZONTAL ===

	// Transition SSAO to input
	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		ssaoImg.image,
		ssaoImg.format,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// Transition blur h for output
	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		ssaoBlur.image,
		ssaoBlur.format,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_GENERAL);


	// Push writing for blur horizontal
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_1_TEX,
		ssaoImg.imageView,
		ssaoSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_1_TEX,
		ssaoBlur.imageView
	);

	ssaoPush.blurDirection = { 1.0f, 0.0f }; // Horizontal
	RenderPasses::dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::SSAOBlur),
		ssaoScope,
		frameCtx.descriptorWriter);

	// ==========================
	// === SSAO BLUR VERTICAL ===

	// Transition blur h for input
	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		ssaoBlur.image,
		ssaoBlur.format,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// Transition blur v for outout
	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		ssaoImg.image,
		ssaoImg.format,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_IMAGE_LAYOUT_GENERAL);

	// Push writing for blur vertical
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_1_TEX,
		ssaoBlur.imageView,
		ssaoSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_1_TEX,
		ssaoImg.imageView
	);

	ssaoPush.blurDirection = { 0.0f, 1.0f }; // Vertical
	RenderPasses::dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::SSAOBlur),
		ssaoScope,
		frameCtx.descriptorWriter);

	// Final transition before lighting
	// ssaoBlurV final output
	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		ssaoImg.image,
		ssaoImg.format,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);
}

void RenderPasses::volumetricLightingPass(FrameContext& frameCtx, ComputeDispatchScope volLightScope, Profiler& profiler) {
	auto& depthResolved = ResourceManager::getDepthResolvedImage();
	auto& volNoiseTex = ResourceManager::getVolumetricNoiseImage();
	auto& volumetricLight = ResourceManager::getVolumetricLightImage();
	auto& volumetricBlur = ResourceManager::getVolumetricBlurImage();

	auto nearestClampSampler = ResourceManager::getNearestClampSampler();
	auto noiseSampler = ResourceManager::getNoiseSampler();
	auto linearClampSampler = ResourceManager::getLinearClampSampler();

	auto& volLightPush = profiler.volLightSettings;

	// === VOLUMETRIC LIGHT RAY MARCH ===

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		volumetricLight.image,
		volumetricLight.format,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_GENERAL
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_DEPTH_TEX,
		depthResolved.imageView,
		nearestClampSampler,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_NOISE_TEX,
		volNoiseTex.imageView,
		noiseSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_1_TEX,
		volumetricLight.imageView
	);

	RenderPasses::dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::VolumetricLight),
		volLightScope,
		frameCtx.descriptorWriter);

	// === BLUR HORIZONTAL ===

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		volumetricLight.image,
		volumetricLight.format,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		volumetricBlur.image,
		volumetricBlur.format,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_GENERAL
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_DEPTH_TEX,
		depthResolved.imageView,
		nearestClampSampler,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_1_TEX,
		volumetricLight.imageView,
		linearClampSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_1_TEX,
		volumetricBlur.imageView
	);

	volLightPush.blurDirection = { 1.0f, 0.0f };
	RenderPasses::dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::VolumetricLightBlur),
		volLightScope,
		frameCtx.descriptorWriter);


	// === VERTICAL BLUR ===

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		volumetricLight.image,
		volumetricLight.format,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		VK_IMAGE_LAYOUT_GENERAL
	);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		volumetricBlur.image,
		volumetricBlur.format,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_DEPTH_TEX,
		depthResolved.imageView,
		nearestClampSampler,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_1_TEX,
		volumetricBlur.imageView,
		linearClampSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_1_TEX,
		volumetricLight.imageView
	);

	volLightPush.blurDirection = { 0.0f, 1.0f };
	RenderPasses::dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::VolumetricLightBlur),
		volLightScope,
		frameCtx.descriptorWriter);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		volumetricLight.image,
		volumetricLight.format,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);
}

void RenderPasses::skyboxPass(FrameContext& frameCtx, const PipelineHandle& pipeHandle, Profiler& profiler) {
	vkCmdBindPipeline(
		frameCtx.cmdBuffer,
		pipeHandle.bindPoint,
		pipeHandle.pipeline);

	const auto& sceneData = RenderScene::getCurrentSceneData();

	glm::mat4 view = glm::mat4(glm::mat3(sceneData.view)); // strip translation
	glm::mat4 viewproj = sceneData.proj * view;
	glm::mat4 invVp = glm::inverse(viewproj);

	bindPushConstants(invVp, frameCtx.cmdBuffer);

	vkCmdDraw(frameCtx.cmdBuffer, 3, 1, 0, 0);

	// Literally one triangle
	if (profiler.debugToggles.enableStats) {
		profiler.addDirect(1, 1);
	}
}

void RenderPasses::opaqueMeshPass(FrameContext& frameCtx, const PipelineHandle& pipeHandle, Profiler& profiler) {
	vkCmdBindPipeline(frameCtx.cmdBuffer, pipeHandle.bindPoint, pipeHandle.pipeline);

	frameCtx.descriptorWriter.updatePushSet(
		frameCtx.cmdBuffer,
		pipeHandle.bindPoint,
		Pipelines::_globalLayout.layout);

	vkCmdDrawIndexedIndirect(frameCtx.cmdBuffer,
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
		frameCtx.cmdBuffer,
		pipeHandle.bindPoint,
		pipeHandle.pipeline);

	vkCmdDrawIndexedIndirect(frameCtx.cmdBuffer,
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
		frameCtx.cmdBuffer,
		pipeHandle.bindPoint,
		pipeHandle.pipeline);

	const VkDeviceSize vtxOffset = 0;
	vkCmdBindVertexBuffers(frameCtx.cmdBuffer, 0, 1, &obbVBO.buffer, &vtxOffset);

	static struct alignas(16) OBBPush {
		VkDeviceAddress vertexBuffer;
		uint32_t pad0[2];
	} pc{};
	pc.vertexBuffer = obbVBO.address;

	bindPushConstants(pc, frameCtx.cmdBuffer);

	for (uint32_t i = 0; i < drawOffsets.size(); ++i) {
		uint32_t vertexOffset = drawOffsets[i];
		vkCmdDraw(frameCtx.cmdBuffer, vertsLineCount, 1, vertexOffset, 0);
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
	std::array<glm::vec3, MAX_SHADOW_CASCADES * 8> worldCorners{};
	std::array<glm::vec3, MAX_SHADOW_CASCADES * 24> lineVerts{};

	for (uint32_t i = 0; i < MAX_SHADOW_CASCADES; ++i) {
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
		frameCtx.cmdBuffer,
		pipeHandle.bindPoint,
		pipeHandle.pipeline);

	const VkDeviceSize vtxOffset = 0;
	vkCmdBindVertexBuffers(frameCtx.cmdBuffer, 0, 1, &cascadeVPVBO.buffer, &vtxOffset);

	static struct alignas(16) CascadeVPPush {
		VkDeviceAddress cascadeVPVertBuffer;
		uint32_t pad0[2];
	} pc{};
	pc.cascadeVPVertBuffer = cascadeVPVBO.address;

	bindPushConstants(pc, frameCtx.cmdBuffer);

	vkCmdDraw(frameCtx.cmdBuffer, static_cast<uint32_t>(lineVerts.size()), 1, 0, 0);

	if (profiler.debugToggles.enableStats) {
		profiler.addDirect(1);
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
// Can take in a dummy writer
void RenderPasses::dispatchComputePass(
	VkCommandBuffer cmd,
	const PipelineHandle& pipeHandle,
	ComputeDispatchScope& scope,
	DescriptorWriter& writer)
{
	vkCmdBindPipeline(cmd, pipeHandle.bindPoint, pipeHandle.pipeline);

	if (scope.pushData && scope.pushSize > 0) {
		const PipelineLayoutConst globalLayout = Pipelines::_globalLayout;
		vkCmdPushConstants(
			cmd,
			globalLayout.layout,
			globalLayout.pcRange.stageFlags,
			globalLayout.pcRange.offset,
			static_cast<uint32_t>(scope.pushSize),
			scope.pushData);
	}

	if (writer.enablePushDescriptor) {
		writer.updatePushSet(
			cmd,
			pipeHandle.bindPoint,
			Pipelines::_globalLayout.layout);
	}

	scope.calculateGroups();

	vkCmdDispatch(
		cmd,
		scope.groupCountX,
		scope.groupCountY,
		scope.groupCountZ);
}