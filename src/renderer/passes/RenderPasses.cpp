#include "pch.h"

#include "RenderPasses.h"
#include "renderer/scene/Visibility.h"
#include "utils/BufferUtils.h"
#include "renderer/scene/RenderScene.h"
#include "engine/Engine.h"
#include "renderer/Renderer.h"

// TODO: REDESIGN ALL OF THIS

static constexpr VkDeviceSize drawCmdSize = sizeof(VkDrawIndexedIndirectCommand);
static constexpr uint32_t vertsLineCount = 24u;
static constexpr VkDeviceSize DISPATCH_SLOT_STRIDE_BYTES = 16u;

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

void RenderPasses::shadowCSMPass(
	FrameContext& frameCtx,
	GraphicsScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.cmdBuffer,
		scope.passID
	);

	auto& shadowImg = ResourceManager::getDirectionalCSMAtlas();

	const VkExtent2D atlasExtent = { shadowImg.extent.width, shadowImg.extent.height };

	VkExtent2D tileExtent{};
	tileExtent.width = atlasExtent.width / 2u;
	tileExtent.height = atlasExtent.height / 2u;

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		shadowImg,
		VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	AttachmentDesc shadowDepth{};
	shadowDepth.imageView = shadowImg.imageView;
	shadowDepth.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	shadowDepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	shadowDepth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	shadowDepth.clearValue.depthStencil.depth = 1.0f;

	scope.atlasOn = true;
	scope.atlasExtent = tileExtent;

	auto& cascadeVP = RenderScene::getShadowCSM().cascadeVP;

	auto& pipeline = Pipelines::getHandle(PipelineID::Shadow);

	for (uint32_t cascadeIdx = 0; cascadeIdx < MAX_SHADOW_CASCADES; ++cascadeIdx) {
		const uint32_t tileX = cascadeIdx % 2u;
		const uint32_t tileY = cascadeIdx / 2u;

		scope.atlasOffset.x = static_cast<int32_t>(tileX * tileExtent.width);
		scope.atlasOffset.y = static_cast<int32_t>(tileY * tileExtent.height);

		beginRendering(
			frameCtx.cmdBuffer,
			{ shadowDepth },
			tileExtent,
			scope);

		bindPushConstants(cascadeVP[cascadeIdx], frameCtx.cmdBuffer);

		vkCmdBindPipeline(
			frameCtx.cmdBuffer,
			pipeline.bindPoint,
			pipeline.pipeline);

		vkCmdDrawIndexedIndirect(
			frameCtx.cmdBuffer,
			frameCtx.indirectDraws_GPU.buffer,
			frameCtx.shadowDrawRanges[cascadeIdx].firstCommand * drawCmdSize,
			frameCtx.shadowDrawRanges[cascadeIdx].commandCount,
			drawCmdSize);

		endRendering(frameCtx.cmdBuffer);

		if (profiler.debugToggles.enableProfilerView) {
			profiler.getStats().directionalCSMIndirect.commands += 1;
			profiler.getStats().directionalCSMIndirect.subdraws += frameCtx.shadowDrawRanges[cascadeIdx].firstCommand * drawCmdSize;
		}
	}

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		shadowImg,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
}

void RenderPasses::shadowFlashLightPass(
	FrameContext& frameCtx,
	GraphicsScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.cmdBuffer,
		scope.passID
	);

	auto& flashlightShadow = ResourceManager::getFlashLightShadowMap();

	const VkExtent2D extent = { flashlightShadow.extent.width, flashlightShadow.extent.height };

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		flashlightShadow,
		VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	AttachmentDesc shadowDepth{};
	shadowDepth.imageView = flashlightShadow.imageView;
	shadowDepth.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	shadowDepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	shadowDepth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	shadowDepth.clearValue.depthStencil.depth = 1.0f;

	beginRendering(
		frameCtx.cmdBuffer,
		{ shadowDepth },
		extent,
		scope);

	bindPushConstants(LightingSystem::_mainFlashLight.viewProj, frameCtx.cmdBuffer);

	auto& pipeline = Pipelines::getHandle(PipelineID::Shadow);

	vkCmdBindPipeline(
		frameCtx.cmdBuffer,
		pipeline.bindPoint,
		pipeline.pipeline);

	vkCmdDrawIndexedIndirect(
		frameCtx.cmdBuffer,
		frameCtx.indirectDraws_GPU.buffer,
		frameCtx.flashLightShadowRange.firstCommand * drawCmdSize,
		frameCtx.flashLightShadowRange.commandCount,
		drawCmdSize);

	endRendering(frameCtx.cmdBuffer);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		flashlightShadow,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);

	if (profiler.debugToggles.enableProfilerView) {
		profiler.getStats().flashlightShadowIndirect.commands += 1;
		profiler.getStats().flashlightShadowIndirect.subdraws += frameCtx.flashLightShadowRange.firstCommand * drawCmdSize;
	}
}

void RenderPasses::BasePrepass(
	FrameContext& frameCtx,
	GraphicsScope scope,
	Profiler& profiler,
	const bool isTemporalValid)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.cmdBuffer,
		scope.passID
	);

	auto& depthResolved = ResourceManager::getDepthResolvedImage();
	auto& prevDepthResolved = ResourceManager::getPrevDepthResolvedImage();
	auto& normals = ResourceManager::getViewSpaceNormals();
	auto& velocity = ResourceManager::getVelocityImage();
	auto& prevVelocity = ResourceManager::getPrevVelocityImage();

	if (isTemporalValid) {
		ImageUtils::imageCopy(
			frameCtx.cmdBuffer,
			depthResolved,
			prevDepthResolved,
			VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
		ImageUtils::imageCopy(
			frameCtx.cmdBuffer,
			velocity,
			prevVelocity,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
	else {
		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			velocity,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	}

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		depthResolved,
		VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		normals,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);


	AttachmentDesc prepassDepth{};
	prepassDepth.imageView = depthResolved.imageView;
	prepassDepth.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	prepassDepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	prepassDepth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	prepassDepth.clearValue.depthStencil.depth = 0.0f;

	AttachmentDesc prepassNormal{};
	prepassNormal.imageView = normals.imageView;
	prepassNormal.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	prepassNormal.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	prepassNormal.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	prepassNormal.clearValue.color = { { 0.5f, 0.5f, 1.0f, 1.0f } };

	AttachmentDesc prepassVelocity{};
	prepassVelocity.imageView = velocity.imageView;
	prepassVelocity.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	prepassVelocity.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	prepassVelocity.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	prepassVelocity.clearValue.color = { { 0.0f, 0.0f, 0.0f, 0.0f } };

	beginRendering(
		frameCtx.cmdBuffer,
		{ prepassNormal, prepassVelocity, prepassDepth },
		{ depthResolved.extent.width, depthResolved.extent.height },
		scope);

	auto& pipeline = Pipelines::getHandle(PipelineID::Prepass);

	vkCmdBindPipeline(
		frameCtx.cmdBuffer,
		pipeline.bindPoint,
		pipeline.pipeline);

	vkCmdDrawIndexedIndirect(frameCtx.cmdBuffer,
		frameCtx.indirectDraws_GPU.buffer,
		frameCtx.opaqueRange.firstCommand * drawCmdSize,
		frameCtx.opaqueRange.commandCount,
		drawCmdSize);

	endRendering(frameCtx.cmdBuffer);

	// Transition images to be sampled
	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		depthResolved,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		normals,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		velocity,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void RenderPasses::hiZGenerationPass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.cmdBuffer,
		scope.passID
	);

	auto& depthResolved = ResourceManager::getDepthResolvedImage();
	auto& dummyUint8 = ResourceManager::getDummyUint8();
	auto nearestClampSampler = ResourceManager::getNearestClampSampler();
	auto& hiZ = ResourceManager::getHiZ();
	auto hiZSampler = ResourceManager::getHiZSampler();

	// Transition all mips to GENERAL for compute writes
	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		hiZ,
		VK_IMAGE_LAYOUT_GENERAL,
		0,                         // Start at base mip
		hiZ.mipLevelCount,         // All levels transitioned
		VK_IMAGE_LAYOUT_UNDEFINED
	);

	// First ever transition
	if (dummyUint8.previousLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			dummyUint8,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		);
	}

	// Uses default workgroup size 8x8x1

	struct alignas(16) DepthPyramidPush {
		uint32_t mipLevel;
		float pad0;
		glm::vec2 invSize; // 1.0 / output mip res
	} push{};

	scope.setPush(push);

	VkExtent3D srcExtent = hiZ.extent; // Start at full res
	VkExtent3D dstExtent = hiZ.extent; // updated after each iteration

	for (uint32_t mip = 0; mip < hiZ.mipLevelCount; ++mip) {

		// Write to this mip
		VkImageView dstView = hiZ.storageViews[mip];

		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_INPUT_1_TEX,
			depthResolved.imageView,
			nearestClampSampler,
			VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);

		if (mip > 0) {
			frameCtx.descriptorWriter.writePushImage(
				PUSH_BINDING_INPUT_2_TEX,
				hiZ.storageViews[static_cast<size_t>(mip - 1u)],
				hiZSampler);
		}
		else {
			// Empty image for first copy stage at mip 0
			frameCtx.descriptorWriter.writePushImage(
				PUSH_BINDING_INPUT_2_TEX,
				dummyUint8.imageView,
				hiZSampler);
		}
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_OUTPUT_1_TEX,
			dstView);

		push.mipLevel = mip;
		push.invSize = {
			1.0f / static_cast<float>(srcExtent.width),
			1.0f / static_cast<float>(srcExtent.height)
		};

		scope.extent = { dstExtent.width, dstExtent.height };

		dispatchComputePass(
			frameCtx.cmdBuffer,
			Pipelines::getHandle(PipelineID::HiZGen),
			scope,
			frameCtx.descriptorWriter);

		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			hiZ,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			mip, // Current mip transition
			1,   // How many mips to transition in this case one
			VK_IMAGE_LAYOUT_GENERAL
		);

		srcExtent = dstExtent;

		// Next mip size
		dstExtent.width = std::max(1u, dstExtent.width >> 1);
		dstExtent.height = std::max(1u, dstExtent.height >> 1);
	}
}

void RenderPasses::GTAOPass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler,
	const bool isTemporalValid)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.cmdBuffer,
		scope.passID
	);
	auto& depthPyramid = ResourceManager::getHiZ();
	auto& normal = ResourceManager::getViewSpaceNormals();
	auto& rawAO = ResourceManager::getAORawImage();
	auto& aoTemp = ResourceManager::getAOTempImage();
	auto& edgeInfo = ResourceManager::getEdgeInfoImage();
	auto& bentNormals = ResourceManager::getBentNormals();

	auto nearestClampSampler = ResourceManager::getNearestClampSampler();
	auto depthPyramidSampler = ResourceManager::getHiZSampler();
	auto aoSampler = ResourceManager::getLinearLODClampSampler();
	auto nearSampler = ResourceManager::getDefaultSamplerNearest();

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		rawAO,
		VK_IMAGE_LAYOUT_GENERAL);
	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		edgeInfo,
		VK_IMAGE_LAYOUT_GENERAL);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		bentNormals,
		VK_IMAGE_LAYOUT_GENERAL);

	// =================
	// === GTAO MAIN ===

	// Inputs
	// Depth pyramid
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_1_TEX,
		depthPyramid.imageView,
		depthPyramidSampler);
	// Normals
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_2_TEX,
		normal.imageView,
		nearestClampSampler);

	// Outputs
	// raw ao
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_1_TEX,
		rawAO.imageView);
	// edge info
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_2_TEX,
		edgeInfo.imageView);
	// bent normals
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_3_TEX,
		bentNormals.imageView);


	dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::GTAO),
		scope,
		frameCtx.descriptorWriter);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		bentNormals,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// ==============================
	// === GTAO FILTER HORIZONTAL ===

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		rawAO,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// Transition edge info once for both filter passes
	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		edgeInfo,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		aoTemp,
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

	scope.editPush<GTAOPush>(
		[](GTAOPush& push)
		{
			push.blurDirection = { 1.0f, 0.0f }; // Horizontal
		});

	dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::GTAOFilter),
		scope,
		frameCtx.descriptorWriter);

	// ============================
	// === GTAO FILTER VERTICAL ===

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		aoTemp,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		rawAO,
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

	scope.editPush<GTAOPush>(
		[](GTAOPush& push)
		{
			push.blurDirection = { 0.0f, 1.0f }; // Vertical
		});
	dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::GTAOFilter),
		scope,
		frameCtx.descriptorWriter);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		rawAO,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);
}

void RenderPasses::screenSpaceContactShadowsPass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.cmdBuffer,
		scope.passID
	);

	auto& depthResolved = ResourceManager::getDepthResolvedImage();

	auto& finalShadowMask = ResourceManager::getScreenSpaceShadowMask();
	const auto pointSampler = ResourceManager::getPointBorderSampler();

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		finalShadowMask,
		VK_IMAGE_LAYOUT_GENERAL
	);

	const auto& list = RenderScene::_dispatchListSSS;

	const auto& pixelSizes = RenderScene::getCurrentSceneData().pixelSizes;
	glm::vec2 invSize = glm::vec2(pixelSizes.x, pixelSizes.y);

	scope.editPush<SSSPush>(
		[invSize, list](SSSPush& push)
		{
			push.lightCoords = list.lightCoords;
			push.invDepthSize = invSize;
		});

	for (int i = 0; i < list.dispatchCount; i++) {
		const DispatchData& disp = list.dispatch[i];

		scope.editPush<SSSPush>(
			[disp](SSSPush& push)
			{
				push.waveOffsets = disp.waveOffset;
			});

		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_INPUT_1_TEX,
			depthResolved.imageView,
			pointSampler,
			VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
		);
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_OUTPUT_1_TEX,
			finalShadowMask.imageView
		);

		scope.groupCountX = disp.waveCount[0];
		scope.groupCountY = disp.waveCount[1];
		scope.groupCountZ = disp.waveCount[2];

		dispatchComputePass(
			frameCtx.cmdBuffer,
			Pipelines::getHandle(PipelineID::ScreenSpaceContactShadows),
			scope,
			frameCtx.descriptorWriter
		);
	}

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		finalShadowMask,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);
}

void RenderPasses::volumetricLightingPass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.cmdBuffer,
		scope.passID
	);
	auto& depthResolved = ResourceManager::getDepthResolvedImage();
	auto& volumetricLight = ResourceManager::getVolumetricLightImage();
	auto& volumetricBlur = ResourceManager::getVolumetricBlurImage();

	const auto nearestClampSampler = ResourceManager::getNearestClampSampler();
	const auto linearClampSampler = ResourceManager::getLinearClampSampler();

	// === VOLUMETRIC LIGHT RAY MARCH ===

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		volumetricLight,
		VK_IMAGE_LAYOUT_GENERAL
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_1_TEX,
		depthResolved.imageView,
		nearestClampSampler,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_1_TEX,
		volumetricLight.imageView
	);

	dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::VolumetricLight),
		scope,
		frameCtx.descriptorWriter);

	// === BLUR HORIZONTAL ===

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		volumetricLight,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		volumetricBlur,
		VK_IMAGE_LAYOUT_GENERAL
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_1_TEX,
		depthResolved.imageView,
		nearestClampSampler,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_2_TEX,
		volumetricLight.imageView,
		linearClampSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_1_TEX,
		volumetricBlur.imageView
	);

	scope.editPush<VolumetricPush>(
		[](VolumetricPush& push)
		{
			push.blurDirection = { 1.0f, 0.0f }; // Horizontal
		});

	dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::VolumetricLightBlur),
		scope,
		frameCtx.descriptorWriter);


	// === VERTICAL BLUR ===

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		volumetricLight,
		VK_IMAGE_LAYOUT_GENERAL
	);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		volumetricBlur,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_1_TEX,
		depthResolved.imageView,
		nearestClampSampler,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_2_TEX,
		volumetricBlur.imageView,
		linearClampSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_1_TEX,
		volumetricLight.imageView
	);

	scope.editPush<VolumetricPush>(
		[](VolumetricPush& push)
		{
			push.blurDirection = { 0.0f, 1.0f }; // Vertical
		});

	dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::VolumetricLightBlur),
		scope,
		frameCtx.descriptorWriter);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		volumetricLight,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);
}

void RenderPasses::exposurePass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler,
	const AllocatedBuffer& luminanceBuf,
	const bool transparentVisible,
	const bool hasVisibles,
	const bool isTemporalValid)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.cmdBuffer,
		scope.passID
	);

	AllocatedImage opaque{};
	if (profiler.debugToggles.aaMode == AA_TAA && isTemporalValid && hasVisibles) {
		opaque = ResourceManager::getColorHistoryWrite();
	}
	else {
		opaque = ResourceManager::getOpaqueImage();
	}

	auto& transparent = ResourceManager::getTransparentImage();
	auto& dummy = ResourceManager::getDummyImage();
	const auto linearSampler = ResourceManager::getDefaultSamplerLinear();

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_1_TEX,
		opaque.imageView,
		linearSampler);

	if (transparentVisible) {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_INPUT_2_TEX,
			transparent.imageView,
			linearSampler);
	}
	else {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_INPUT_2_TEX,
			dummy.imageView,
			linearSampler);
	}

	// === EXPOSURE REDUCE ===
	dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::ExposureReduce),
		scope,
		frameCtx.descriptorWriter);

	BarrierUtils::bufferComputeWriteToComputeRead(frameCtx.cmdBuffer, luminanceBuf);

	// === EXPOSURE FINALIZE ===
	uint32_t tilesX = scope.extent.width / 16u;
	uint32_t tilesY = scope.extent.height / 16u;
	uint32_t totalTiles = tilesX * tilesY;
	scope.extent.width = totalTiles; // total number of luminance tiles
	scope.extent.height = 1u;
	scope.workgroupSize = { 256u, 1u, 1u };

	struct alignas(16) ExposurePush {
		uint32_t totalTiles;
		uint32_t pad0[3]{};
	} expPush{};
	expPush.totalTiles = totalTiles;
	scope.setPush(totalTiles);

	dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::ExposureFinalize),
		scope,
		frameCtx.descriptorWriter);

	BarrierUtils::bufferComputeWriteToComputeRead(frameCtx.cmdBuffer, luminanceBuf);
}

void RenderPasses::finalCompositePass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler,
	const bool transparentVisible,
	const bool hasVisibles,
	const bool isTemporalValid)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.cmdBuffer,
		scope.passID
	);

	AllocatedImage opaque{};
	if (profiler.debugToggles.aaMode == AA_TAA && isTemporalValid && hasVisibles) {
		opaque = ResourceManager::getColorHistoryWrite();
	}
	else {
		opaque = ResourceManager::getOpaqueImage();
	}

	auto& transparent = ResourceManager::getTransparentImage();
	auto& dummy = ResourceManager::getDummyImage();
	auto& toneMap = ResourceManager::getToneMapImage();

	const auto linearSampler = ResourceManager::getDefaultSamplerLinear();
	auto& volLight = ResourceManager::getVolumetricLightImage();
	const auto linearClampSampler = ResourceManager::getLinearClampSampler();
	auto& lensFlareColor = ResourceManager::getLensFlareColorImage();

	auto& cmaa2 = ResourceManager::getAAColor();

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		toneMap,
		VK_IMAGE_LAYOUT_GENERAL);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_1_TEX,
		toneMap.imageView);

	if (profiler.debugToggles.aaMode == AA_CMAA2) {
		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			cmaa2,
			VK_IMAGE_LAYOUT_GENERAL);

		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_OUTPUT_2_TEX,
			cmaa2.imageView);
	}
	else {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_OUTPUT_2_TEX,
			toneMap.imageView);
	}

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_1_TEX,
		opaque.imageView,
		linearSampler);


	if (transparentVisible) {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_INPUT_2_TEX,
			transparent.imageView,
			linearSampler);
	}
	else {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_INPUT_2_TEX,
			dummy.imageView,
			linearSampler);
	}

	if (hasVisibles && profiler.debugToggles.enableVolumetrics && profiler.debugToggles.enableShadows) {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_INPUT_3_TEX,
			volLight.imageView,
			linearClampSampler);
	}
	else {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_INPUT_3_TEX,
			dummy.imageView,
			linearClampSampler);
	}

	if (hasVisibles && profiler.debugToggles.enableLensFlare) {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_INPUT_4_TEX,
			lensFlareColor.imageView,
			linearClampSampler);
	}
	else {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_INPUT_4_TEX,
			dummy.imageView,
			linearClampSampler);
	}

	dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::FinalComposite),
		scope,
		frameCtx.descriptorWriter);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		toneMap,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void RenderPasses::chromaticAberrationPass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler,
	const bool hasVisibles)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.cmdBuffer,
		scope.passID
	);

	auto& aaColor = ResourceManager::getAAColor();
	auto& tonemap = ResourceManager::getToneMapImage();
	auto& finalComposite = ResourceManager::getPostNonAAComposite();
	const auto linearClampSampler = ResourceManager::getLinearClampSampler();

	if (hasVisibles && (profiler.debugToggles.aaMode != AA_OFF && profiler.debugToggles.aaMode != AA_TAA)) {
		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			aaColor,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		);

		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_INPUT_1_TEX,
			aaColor.imageView,
			linearClampSampler
		);
	}
	else {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_INPUT_1_TEX,
			tonemap.imageView,
			linearClampSampler
		);
	}

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		finalComposite,
		VK_IMAGE_LAYOUT_GENERAL
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_1_TEX,
		finalComposite.imageView
	);

	dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::ChromaticAberration),
		scope,
		frameCtx.descriptorWriter
	);
}

void RenderPasses::lensFlarePass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler,
	const bool transparentVisible,
	const bool hasVisibles,
	const bool isTemporalValid)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.cmdBuffer,
		scope.passID
	);
	AllocatedImage opaque{};
	if (profiler.debugToggles.aaMode == AA_TAA && isTemporalValid && hasVisibles) {
		opaque = ResourceManager::getColorHistoryWrite();
	}
	else {
		opaque = ResourceManager::getOpaqueImage();
	}
	auto& transparent = ResourceManager::getTransparentImage();
	auto& dummy = ResourceManager::getDummyImage();
	auto& flareBright = ResourceManager::getFlareBrightImage();
	auto& lensFlareColor = ResourceManager::getLensFlareColorImage();
	const auto linearSampler = ResourceManager::getDefaultSamplerLinear();
	auto& volLight = ResourceManager::getVolumetricLightImage();
	const auto linearClampSampler = ResourceManager::getLinearClampSampler();
	auto& hiZ = ResourceManager::getHiZ();
	const auto hiZSampler = ResourceManager::getHiZSampler();

	// Get both lens flare outputs ready
	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		flareBright,
		VK_IMAGE_LAYOUT_GENERAL);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		lensFlareColor,
		VK_IMAGE_LAYOUT_GENERAL);

	// === FLARE BRIGHT STAGE ===
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_1_TEX,
		flareBright.imageView);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_1_TEX,
		opaque.imageView,
		linearSampler);

	if (transparentVisible) {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_INPUT_2_TEX,
			transparent.imageView,
			linearSampler);
	}
	else {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_INPUT_2_TEX,
			dummy.imageView,
			linearSampler);
	}

	if (hasVisibles && profiler.debugToggles.enableVolumetrics && profiler.debugToggles.enableShadows) {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_INPUT_3_TEX,
			volLight.imageView,
			linearClampSampler);
	}
	else {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_INPUT_3_TEX,
			dummy.imageView,
			linearClampSampler);
	}

	dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::FlareBright),
		scope,
		frameCtx.descriptorWriter);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		flareBright,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);


	// === LENS FLARE COLOR STAGE ===
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_1_TEX,
		lensFlareColor.imageView);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_1_TEX,
		hiZ.imageView,
		hiZSampler);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_2_TEX,
		flareBright.imageView,
		linearClampSampler);

	dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::FlareGen),
		scope,
		frameCtx.descriptorWriter);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		lensFlareColor,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void RenderPasses::clusteredPass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.cmdBuffer,
		scope.passID
	);
	// Reset buffers
	vkCmdFillBuffer(
		frameCtx.cmdBuffer,
		frameCtx.visibleLightCount_GPU.buffer,
		0u,
		frameCtx.visibleLightCount_GPU.info.size,
		0u
	);
	vkCmdFillBuffer(
		frameCtx.cmdBuffer,
		frameCtx.clusterCounts_GPU.buffer,
		0u,
		frameCtx.clusterCounts_GPU.info.size,
		0u
	);
	vkCmdFillBuffer(
		frameCtx.cmdBuffer,
		frameCtx.clusterCursors_GPU.buffer,
		0u,
		frameCtx.clusterCursors_GPU.info.size,
		0u
	);

	vkCmdFillBuffer(
		frameCtx.cmdBuffer,
		frameCtx.clusterScanScratch_GPU.buffer,
		0u,
		frameCtx.clusterScanScratch_GPU.info.size,
		0u
	);

	VkDeviceSize dispatchLightOffsetBytes = INDIRECT_DISPATCH_SLOT_LIGHTS * DISPATCH_SLOT_STRIDE_BYTES;
	VkDeviceSize dispatchClusterOffsetBytes = INDIRECT_DISPATCH_SLOT_CLUSTERS * DISPATCH_SLOT_STRIDE_BYTES;

	vkCmdFillBuffer(
		frameCtx.cmdBuffer,
		frameCtx.dispatchIndirectArgs_GPU.buffer,
		dispatchLightOffsetBytes,
		DISPATCH_SLOT_STRIDE_BYTES + DISPATCH_SLOT_STRIDE_BYTES,
		0u
	);

	BarrierUtils::bufferFillToComputeRW(frameCtx.cmdBuffer, frameCtx.visibleLightCount_GPU);
	BarrierUtils::bufferFillToComputeRW(frameCtx.cmdBuffer, frameCtx.clusterCounts_GPU);
	BarrierUtils::bufferFillToComputeRW(frameCtx.cmdBuffer, frameCtx.clusterCursors_GPU);
	BarrierUtils::bufferFillToComputeRW(frameCtx.cmdBuffer, frameCtx.clusterScanScratch_GPU);
	BarrierUtils::bufferFillToComputeRW(frameCtx.cmdBuffer, frameCtx.dispatchIndirectArgs_GPU);

	const auto& hiZ = ResourceManager::getHiZ();
	const auto hiZSampler = ResourceManager::getHiZSampler();

	// === Tile slice ranges ===
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_1_TEX,
		hiZ.imageView,
		hiZSampler);

	dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::ClusterTileSliceRanges),
		scope,
		frameCtx.descriptorWriter);

	BarrierUtils::bufferComputeWriteToComputeRead(frameCtx.cmdBuffer, frameCtx.clusterTileSliceRanges_GPU);

	scope.workgroupSize = { 256u, 1u, 1u };

	auto activeLightCount = LightingSystem::getActiveLightCount();

	// One job per light
	scope.extent = {
		activeLightCount,
		1u
	};

	auto& frusPlanes = RenderScene::getMainFrustum().planes;
	static struct alignas(16) LightCullPush {
		uint32_t activeLightCount;
		uint32_t pad0;
		uint32_t pad1;
		uint32_t pad2;
		glm::vec4 planes[6];
	} pc{};
	 pc.activeLightCount = activeLightCount;
	 std::copy(std::begin(frusPlanes),
		 std::end(frusPlanes),
		 std::begin(pc.planes));

	scope.setPush(pc);

	// === Visible light list ===
	dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::VisibleLightList),
		scope,
		frameCtx.descriptorWriter);

	BarrierUtils::bufferComputeWriteToComputeRead(frameCtx.cmdBuffer, frameCtx.visibleLightCount_GPU);
	BarrierUtils::bufferComputeWriteToComputeRead(frameCtx.cmdBuffer, frameCtx.visibleLightIDs_GPU);

	scope.clearPush();

	scope.extent = { 1u, 1u };
	scope.workgroupSize = { 1u, 1u, 1u };

	// === Dispatch args compute for visible lights/clusters
	dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::IndirectArgsLight),
		scope,
		frameCtx.descriptorWriter
	);

	BarrierUtils::bufferComputeWriteToIndirectDispatchRead(frameCtx.cmdBuffer, frameCtx.dispatchIndirectArgs_GPU);

	// Last passes have data on visible lights and clusters
	scope.setIndirect(frameCtx.dispatchIndirectArgs_GPU.buffer, dispatchLightOffsetBytes);

	// === Cluster counts ===
	dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::ClusterCount),
		scope,
		frameCtx.descriptorWriter);

	BarrierUtils::bufferComputeWriteToComputeRead(frameCtx.cmdBuffer, frameCtx.clusterCounts_GPU);

	scope.setIndirect(frameCtx.dispatchIndirectArgs_GPU.buffer, dispatchClusterOffsetBytes);
	// === scan offsets ===
	dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::ClusterScanOffsets),
		scope,
		frameCtx.descriptorWriter);

	BarrierUtils::bufferComputeWriteToComputeRead(frameCtx.cmdBuffer, frameCtx.clusterOffsets_GPU);
	BarrierUtils::bufferComputeWriteToComputeRead(frameCtx.cmdBuffer, frameCtx.clusterScanScratch_GPU);

	scope.setIndirect(frameCtx.dispatchIndirectArgs_GPU.buffer, dispatchLightOffsetBytes);
	// === scatter ids ===
	dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::ClusterScatterIDs),
		scope,
		frameCtx.descriptorWriter);

	BarrierUtils::bufferComputeWriteToFragmentRead(frameCtx.cmdBuffer, frameCtx.clusterLightIDs_GPU);
}

void RenderPasses::SMAAPass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.cmdBuffer,
		scope.passID
	);
	auto& tonemap = ResourceManager::getToneMapImage();
	auto& smaaColor = ResourceManager::getAAColor();
	auto& smaaEdges = ResourceManager::getSMAAEdges();
	auto& smaaWeights = ResourceManager::getSMAAWeights();
	auto& depthResolved = ResourceManager::getDepthResolvedImage();
	const auto linearSampler = ResourceManager::getLinearLODClampSampler();
	const auto nearestClampSampler = ResourceManager::getNearestClampSampler();

	// Push constant only required for weight blending
	scope.skipPushConstant = true;

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		smaaColor,
		VK_IMAGE_LAYOUT_GENERAL
	);
	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		smaaEdges,
		VK_IMAGE_LAYOUT_GENERAL
	);
	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		smaaWeights,
		VK_IMAGE_LAYOUT_GENERAL
	);


	// SMAA STAGE 1 EDGE CALCULATION
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_1_TEX,
		tonemap.imageView,
		linearSampler
	);
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_2_TEX,
		depthResolved.imageView,
		nearestClampSampler,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
	);
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_1_TEX,
		smaaEdges.imageView
	);

	dispatchComputePass(frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::SMAAEdges),
		scope,
		frameCtx.descriptorWriter);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		smaaEdges,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);

	scope.skipPushConstant = false;

	// SMAA STAGE 2 WEIGHT BLENDING
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_1_TEX,
		smaaEdges.imageView,
		linearSampler
	);
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_1_TEX,
		smaaWeights.imageView
	);

	dispatchComputePass(frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::SMAAWeights),
		scope,
		frameCtx.descriptorWriter);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		smaaWeights,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);

	scope.skipPushConstant = true;

	// SMAA STAGE 3 NEIGHBOURHOOD BLENDING
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_1_TEX,
		tonemap.imageView,
		linearSampler
	);
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_2_TEX,
		smaaWeights.imageView,
		nearestClampSampler
	);
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_1_TEX,
		smaaColor.imageView
	);

	dispatchComputePass(frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::SMAABlend),
		scope,
		frameCtx.descriptorWriter);
}

void RenderPasses::CMAA2Pass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.cmdBuffer,
		scope.passID
	);

	auto& cmaa2Color = ResourceManager::getAAColor();
	auto& cmaa2WorkingEdges = ResourceManager::getCMAA2WorkingEdges();
	auto& tonemap = ResourceManager::getToneMapImage();
	const auto nearestClampSampler = ResourceManager::getNearestClampSampler();
	VkDeviceSize processOffsetBytes = INDIRECT_DISPATCH_SLOT_CMAA2_SHAPES * DISPATCH_SLOT_STRIDE_BYTES;
	VkDeviceSize deferredOffsetBytes = INDIRECT_DISPATCH_SLOT_CMAA2_DEFERRED * DISPATCH_SLOT_STRIDE_BYTES;

	// Reset buffers
	{
		vkCmdFillBuffer(
			frameCtx.cmdBuffer,
			frameCtx.cmaa2Control_GPU.buffer,
			0u,
			frameCtx.cmaa2Control_GPU.info.size,
			0u
		);
		vkCmdFillBuffer(
			frameCtx.cmdBuffer,
			frameCtx.cmaa2DeferredHeads_GPU.buffer,
			0u,
			frameCtx.cmaa2DeferredHeads_GPU.info.size,
			0x7FFFFFFFu
		);

		BarrierUtils::bufferFillToComputeRW(frameCtx.cmdBuffer, frameCtx.cmaa2Control_GPU);
		BarrierUtils::bufferFillToComputeRW(frameCtx.cmdBuffer, frameCtx.cmaa2DeferredHeads_GPU);
	}

	// BUILD EDGES
	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		cmaa2WorkingEdges,
		VK_IMAGE_LAYOUT_GENERAL
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_1_TEX,
		tonemap.imageView,
		nearestClampSampler
	);
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_1_TEX,
		cmaa2WorkingEdges.imageView
	);

	dispatchComputePass(frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::CMAA2Edges),
		scope,
		frameCtx.descriptorWriter);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		cmaa2WorkingEdges,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);

	BarrierUtils::bufferComputeWriteToComputeRead(frameCtx.cmdBuffer, frameCtx.cmaa2Control_GPU);
	BarrierUtils::bufferComputeWriteToComputeRead(frameCtx.cmdBuffer, frameCtx.cmaa2ShapeCandidates_GPU);


	// COMPUTE DISPATCH ARGS 1

	struct alignas(16) DispatchArgsPush {
		uint32_t pass;
		uint32_t pad0;
		uint32_t pad1;
		uint32_t pad2;
	} argsPush{};
	argsPush.pass = 0;
	scope.extent = { 1u, 1u };
	scope.workgroupSize = { 1u, 1u, 1u };

	scope.setPush(argsPush);

	dispatchComputePass(frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::CMAA2DispatchArgs),
		scope,
		frameCtx.descriptorWriter);

	BarrierUtils::bufferComputeWriteToIndirectDispatchRead(frameCtx.cmdBuffer, frameCtx.dispatchIndirectArgs_GPU);


	// PROCESS CANDIDATES

	scope.setPush(frameCtx.cmaa2Push);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_1_TEX,
		tonemap.imageView,
		nearestClampSampler
	);
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_2_TEX,
		cmaa2WorkingEdges.imageView,
		nearestClampSampler
	);

	scope.setIndirect(frameCtx.dispatchIndirectArgs_GPU.buffer, processOffsetBytes);

	dispatchComputePass(frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::CMAA2ShapeCandidates),
		scope,
		frameCtx.descriptorWriter);

	BarrierUtils::bufferComputeWriteToComputeRead(frameCtx.cmdBuffer, frameCtx.cmaa2Control_GPU);
	BarrierUtils::bufferComputeWriteToComputeRead(frameCtx.cmdBuffer, frameCtx.cmaa2DeferredLocations_GPU);
	BarrierUtils::bufferComputeWriteToComputeRead(frameCtx.cmdBuffer, frameCtx.cmaa2DeferredItems_GPU);
	BarrierUtils::bufferComputeWriteToComputeRead(frameCtx.cmdBuffer, frameCtx.cmaa2DeferredHeads_GPU);

	scope.clearIndirect();

	// COMPUTE DISPATCH ARGS 2
	argsPush.pass = 1;
	scope.setPush(argsPush);

	dispatchComputePass(frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::CMAA2DispatchArgs),
		scope,
		frameCtx.descriptorWriter);

	BarrierUtils::bufferComputeWriteToIndirectDispatchRead(frameCtx.cmdBuffer, frameCtx.dispatchIndirectArgs_GPU);

	// DEFERRED COLOR APPLY
	scope.setPush(frameCtx.cmaa2Push);
	scope.setIndirect(frameCtx.dispatchIndirectArgs_GPU.buffer, deferredOffsetBytes);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_1_TEX,
		tonemap.imageView,
		nearestClampSampler
	);
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_2_TEX,
		cmaa2WorkingEdges.imageView,
		nearestClampSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_1_TEX,
		cmaa2Color.imageView
	);

	dispatchComputePass(frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::CMAA2DeferredResolve),
		scope,
		frameCtx.descriptorWriter);
}

void RenderPasses::TAAPass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.cmdBuffer,
		scope.passID
	);

	auto& opaqueColor = ResourceManager::getOpaqueImage();
	auto& colorHistoryRead = ResourceManager::getColorHistoryRead();
	auto& colorHistoryWrite = ResourceManager::getColorHistoryWrite();
	auto& depthResolved = ResourceManager::getDepthResolvedImage();
	auto& prevDepthResolved = ResourceManager::getPrevDepthResolvedImage();
	auto& velocity = ResourceManager::getVelocityImage();
	auto& prevVelocity = ResourceManager::getPrevVelocityImage();

	const auto nearestClampSampler = ResourceManager::getNearestClampSampler();
	const auto taaSampler = ResourceManager::getTaaHistorySampler();

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		colorHistoryRead,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		colorHistoryWrite,
		VK_IMAGE_LAYOUT_GENERAL
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_1_TEX,
		opaqueColor.imageView,
		nearestClampSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_2_TEX,
		colorHistoryRead.imageView,
		taaSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_3_TEX,
		velocity.imageView,
		nearestClampSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_4_TEX,
		prevVelocity.imageView,
		taaSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_5_TEX,
		depthResolved.imageView,
		nearestClampSampler,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
	);
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_6_TEX,
		prevDepthResolved.imageView,
		nearestClampSampler,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_1_TEX,
		colorHistoryWrite.imageView
	);

	dispatchComputePass(frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::TAA),
		scope,
		frameCtx.descriptorWriter);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		colorHistoryWrite,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);

	ResourceManager::flipColorHistory();
}

void RenderPasses::FXAAPass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.cmdBuffer,
		scope.passID
	);

	auto& fxaaColor = ResourceManager::getAAColor();

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		fxaaColor,
		VK_IMAGE_LAYOUT_GENERAL
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_INPUT_1_TEX,
		ResourceManager::getToneMapImage().imageView,
		ResourceManager::getLinearLODClampSampler()
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_OUTPUT_1_TEX,
		fxaaColor.imageView
	);

	dispatchComputePass(frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::FXAA),
		scope,
		frameCtx.descriptorWriter);
}

void RenderPasses::skyboxPass(
	FrameContext& frameCtx,
	GraphicsScope scope,
	Profiler& profiler,
	const bool hasVisibles)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.cmdBuffer,
		scope.passID
	);
	auto& pipeline = Pipelines::getHandle(PipelineID::Skybox);
	vkCmdBindPipeline(
		frameCtx.cmdBuffer,
		pipeline.bindPoint,
		pipeline.pipeline);

	const auto& sceneData = RenderScene::getCurrentSceneData();

	glm::mat4 proj{};
	if (profiler.debugToggles.aaMode == AA_TAA && hasVisibles) {
		proj = sceneData.proj;
	}
	else {
		proj = RenderScene::getCurProjUnjittered();
	}

	glm::mat4 view = glm::mat4(glm::mat3(sceneData.view)); // strip translation
	glm::mat4 viewproj = proj * view;
	glm::mat4 invVp = glm::inverse(viewproj);

	bindPushConstants(invVp, frameCtx.cmdBuffer);

	vkCmdDraw(frameCtx.cmdBuffer, 3, 1, 0, 0);

	// Literally one triangle
	if (profiler.debugToggles.enableProfilerView) {
		profiler.addDirect(1, 1);
	}
}

void RenderPasses::opaqueMeshPass(
	FrameContext& frameCtx,
	GraphicsScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.cmdBuffer,
		scope.passID
	);
	PipelineHandle pipeline{};
	if (!profiler.pipeOverride.enabled) {
		pipeline = Pipelines::getHandle(PipelineID::Opaque); // default mesh pipeline
	}
	// Wireframe
	else {
		pipeline = Pipelines::getHandle(profiler.pipeOverride.selectedID);
	}

	vkCmdBindPipeline(
		frameCtx.cmdBuffer,
		pipeline.bindPoint,
		pipeline.pipeline);

	frameCtx.descriptorWriter.updatePushSet(
		frameCtx.cmdBuffer,
		pipeline.bindPoint,
		Pipelines::_globalLayout.layout);

	bindPushConstants(Renderer::getForwardPush(), frameCtx.cmdBuffer);

	vkCmdDrawIndexedIndirect(frameCtx.cmdBuffer,
		frameCtx.indirectDraws_GPU.buffer,
		frameCtx.opaqueRange.firstCommand * drawCmdSize,
		frameCtx.opaqueRange.commandCount,
		drawCmdSize
	);

	if (profiler.debugToggles.enableProfilerView) {
		const uint64_t trisOpaque = sumTrianglesIndirectRange(
			frameCtx.indirectDraws,
			frameCtx.opaqueRange.firstCommand,
			frameCtx.opaqueRange.commandCount,
			pipeline.topology);

		profiler.addOpaqueIndirect(/*commands*/1,
			/*sub-draws*/frameCtx.opaqueRange.visibleCount,
			/*triangles*/trisOpaque);
	}
}

void RenderPasses::transparentMeshPass(
	FrameContext& frameCtx,
	GraphicsScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.cmdBuffer,
		scope.passID
	);
	auto& pipeline = Pipelines::getHandle(PipelineID::Transparent);
	vkCmdBindPipeline(
		frameCtx.cmdBuffer,
		pipeline.bindPoint,
		pipeline.pipeline);

	bindPushConstants(Renderer::getForwardPush(), frameCtx.cmdBuffer);

	vkCmdDrawIndexedIndirect(frameCtx.cmdBuffer,
		frameCtx.indirectDraws_GPU.buffer,
		frameCtx.transparentRange.firstCommand * drawCmdSize,
		frameCtx.transparentRange.commandCount,
		drawCmdSize
	);

	if (profiler.debugToggles.enableProfilerView) {
		const uint64_t trisTransparent = sumTrianglesIndirectRange(
			frameCtx.indirectDraws,
			frameCtx.transparentRange.firstCommand,
			frameCtx.transparentRange.commandCount,
			pipeline.topology);

		profiler.addTransparentIndirect(/*commands*/1,
			/*sub-draws*/frameCtx.transparentRange.visibleCount,
			/*triangles*/trisTransparent);
	}
}

void RenderPasses::obbLinePass(
	FrameContext& frameCtx,
	GraphicsScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.cmdBuffer,
		scope.passID
	);
	std::vector<glm::vec3> allVerts;
	std::vector<uint32_t> drawOffsets;

	auto& resources = Engine::getState().getGPUResources();
	const auto& meshes = resources.getResgisteredMeshes().meshData;

	auto emitOBBVerts = [&](const GPUInstance& inst) {
		const auto& aabb = meshes[inst.meshID].localAABB;
		const auto& matrix = RenderScene::_globalTransforms[inst.transformID];
		auto verts = GetOBBVertices(aabb, matrix);
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

	auto& pipeline = Pipelines::getHandle(PipelineID::OBBLine);
	vkCmdBindPipeline(
		frameCtx.cmdBuffer,
		pipeline.bindPoint,
		pipeline.pipeline);

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
		if (profiler.debugToggles.enableProfilerView) {
			profiler.addDirect(1);
		}
	}
}

void RenderPasses::beginRendering(
	VkCommandBuffer cmd,
	const std::vector<AttachmentDesc>& images,
	VkExtent2D extent,
	GraphicsScope& scope)
{
	scope.colorAttachments.clear();
	scope.hasDepth = false;

	for (size_t i = 0; i < images.size(); i++) {
		const auto& desc = images[i];

		VkRenderingAttachmentInfo att = makeAttachmentInfo(desc);

		if (desc.layout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL ||
			desc.layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
			desc.layout == VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL ||
			desc.layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) {
			scope.depthAttachment = att;
			scope.hasDepth = true;
		}
		else {
			scope.colorAttachments.push_back(att);
		}
	}

	scope.info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	scope.info.renderArea.offset = { 0, 0 };
	scope.info.renderArea.extent = extent;

	if (scope.atlasOn) {
		scope.info.renderArea.offset = scope.atlasOffset;
		scope.info.renderArea.extent = scope.atlasExtent;
	}

	scope.info.layerCount = 1;
	scope.info.colorAttachmentCount = static_cast<uint32_t>(scope.colorAttachments.size());
	scope.info.pColorAttachments = scope.colorAttachments.data();
	scope.info.pDepthAttachment = scope.hasDepth ? &scope.depthAttachment : nullptr;

	vkCmdBeginRendering(cmd, &scope.info);

	if (scope.atlasOn) {
		VulkanUtils::defineViewportAndScissorAtlas(
			cmd,
			scope.atlasOffset,
			scope.atlasExtent);
		return;
	}

	VulkanUtils::defineViewportAndScissor(cmd, extent);
}

// Agnostic for all compute passes
// Can take in a dummy writer
void RenderPasses::dispatchComputePass(
	VkCommandBuffer cmd,
	const PipelineHandle& pipeHandle,
	ComputeScope& scope,
	DescriptorWriter& writer)
{
	vkCmdBindPipeline(cmd, pipeHandle.bindPoint, pipeHandle.pipeline);

	if (scope.pushData && scope.pushSize > 0 && !scope.skipPushConstant) {
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

	if (scope.isIndirect()) {
		vkCmdDispatchIndirect(
			cmd,
			scope.indirect.buffer,
			scope.indirect.offset);
		return;
	}

	scope.calculateGroups();

	vkCmdDispatch(
		cmd,
		scope.groupCountX,
		scope.groupCountY,
		scope.groupCountZ);
}
