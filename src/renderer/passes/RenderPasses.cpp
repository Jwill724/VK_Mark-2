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

	auto& shadowImg = ResourceManager::getDirectionalCSMAtlas_Target();

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

	auto& flashlightShadow = ResourceManager::getFlashLightShadowMap_Target();

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

	auto& depthResolved = ResourceManager::getDepthResolved_Target();
	auto& prevDepthResolved = ResourceManager::getPrevDepthResolved_Target();
	auto& velocity = ResourceManager::getVelocity_Target();
	auto& prevVelocity = ResourceManager::getPrevVelocity_Target();
	auto& viewSpaceNormals = ResourceManager::getViewSpaceNormals_Target();

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
			depthResolved,
			VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			velocity,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	}

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		viewSpaceNormals,
		VK_IMAGE_LAYOUT_GENERAL);

	AttachmentDesc prepassDepth{};
	prepassDepth.imageView = depthResolved.imageView;
	prepassDepth.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	prepassDepth.clearValue.depthStencil.depth = 0.0f;

	AttachmentDesc prepassNormal{};
	prepassNormal.imageView = viewSpaceNormals.imageView;
	prepassNormal.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	prepassNormal.clearValue.color = { { 0.5f, 0.5f, 1.0f, 1.0f } };

	AttachmentDesc prepassVelocity{};
	prepassVelocity.imageView = velocity.imageView;
	prepassVelocity.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
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
		velocity,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		viewSpaceNormals,
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

	auto& depthResolved = ResourceManager::getDepthResolved_Target();
	auto& dummyUint8 = ResourceManager::getDummyUint8_Texture();
	auto nearestClampSampler = ResourceManager::getNearestClamp_Sampler();
	auto& hiZ = ResourceManager::getHiZ_Target();
	auto hiZSampler = ResourceManager::getHiZ_Sampler();

	// Transition all mips to GENERAL for compute writes
	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		hiZ,
		VK_IMAGE_LAYOUT_GENERAL,
		0u,                        // Start at base mip
		hiZ.mipLevelCount          // All levels transitioned
	);

	// First ever transition
	if (dummyUint8.currentLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
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
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_READ_1,
			depthResolved,
			nearestClampSampler);

		if (mip > 0) {
			frameCtx.descriptorWriter.writePushImage(
				PUSH_BINDING_READ_2,
				hiZ,
				hiZSampler,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				(mip - 1u));
		}
		else {
			// Empty image for first copy stage at mip 0
			frameCtx.descriptorWriter.writePushImage(
				PUSH_BINDING_READ_2,
				dummyUint8,
				hiZSampler);
		}
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_WRITE_1,
			hiZ,
			VK_NULL_HANDLE,
			VK_IMAGE_LAYOUT_GENERAL,
			mip);

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
	auto& depthResolved = ResourceManager::getDepthResolved_Target();
	auto& linearizedMinHiZ = ResourceManager::getLinearizedMinHiZ_Target();
	auto& rawAO = ResourceManager::getAORaw_Target();
	auto& aoTemp = ResourceManager::getAOTemp_Target();
	auto& edgeInfo = ResourceManager::getAOEdgeInfo_Target();
	auto& bentNormals = ResourceManager::getBentNormals_Target();
	auto& viewSpaceNormals = ResourceManager::getViewSpaceNormals_Target();

	auto nearestClampSampler = ResourceManager::getNearestClamp_Sampler();
	auto depthPyramidSampler = ResourceManager::getHiZ_Sampler();
	auto aoSampler = ResourceManager::getLinearLODClamp_Sampler();
	auto nearSampler = ResourceManager::getDefaultNearest_Sampler();

	// ============================
	// === GTAO DEPTH PREFILTER ===
	{
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_READ_1,
			depthResolved,
			nearestClampSampler);

		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			linearizedMinHiZ,
			VK_IMAGE_LAYOUT_GENERAL,
			0u,
			linearizedMinHiZ.mipLevelCount);

		uint32_t pushWriteBinding = PUSH_BINDING_WRITE_1;
		for (uint32_t i = 0u; i < HI_Z_MIP_COUNT; i++) {
			ASSERT(pushWriteBinding <= PUSH_BINDING_WRITE_5);

			frameCtx.descriptorWriter.writePushImage(
				pushWriteBinding,
				linearizedMinHiZ,
				VK_NULL_HANDLE,
				VK_IMAGE_LAYOUT_GENERAL,
				i);
			pushWriteBinding++;
		}

		dispatchComputePass(
			frameCtx.cmdBuffer,
			Pipelines::getHandle(PipelineID::GTAODepthPrefilter),
			scope,
			frameCtx.descriptorWriter);

		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			linearizedMinHiZ,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			0u,
			linearizedMinHiZ.mipLevelCount);
	}

	// =================
	// === GTAO MAIN ===

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

	// Inputs
	// Depth pyramid
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_READ_1,
		linearizedMinHiZ,
		depthPyramidSampler);
	// View space normals
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_READ_2,
		viewSpaceNormals,
		nearestClampSampler);

	// Outputs
	// raw ao
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_WRITE_1,
		rawAO);
	// edge info
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_WRITE_2,
		edgeInfo);
	// bent normals
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_WRITE_3,
		bentNormals);

	dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::GTAO),
		scope,
		frameCtx.descriptorWriter);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		bentNormals,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

 
	// Spatial filtering / Denoising initial setup

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		rawAO,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

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
		PUSH_BINDING_READ_1,
		rawAO,
		aoSampler
	);
	// edge info
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_READ_2,
		edgeInfo,
		nearSampler
	);

	// Output
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_WRITE_1,
		aoTemp
	);

	// Bi-lateral blur filter
	if (profiler.debugToggles.aaMode != AA_TAA) {
		// ==============================
		// === GTAO FILTER HORIZONTAL ===

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
			PUSH_BINDING_READ_1,
			aoTemp,
			aoSampler
		);
		// edge info
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_READ_2,
			edgeInfo,
			nearSampler
		);

		// Output
		// final filtered ao
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_WRITE_1,
			rawAO
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
	}
	// XeGTAO Denoise (TAA required)
	else {
		// ===========================
		// === GTAO DENOISE PASS 1 ===

		// Works over 2x1 pixels
		scope.workgroupSize = { 32u, 16u, 1u };

		dispatchComputePass(
			frameCtx.cmdBuffer,
			Pipelines::getHandle(PipelineID::GTAODenoise),
			scope,
			frameCtx.descriptorWriter);

		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			aoTemp,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			rawAO,
			VK_IMAGE_LAYOUT_GENERAL);


		// ===========================
		// === GTAO DENOISE PASS 2 ===

		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_READ_1,
			aoTemp,
			aoSampler
		);
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_READ_2,
			edgeInfo,
			nearSampler
		);

		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_WRITE_1,
			rawAO
		);

		scope.editPush<GTAOPush>(
			[](GTAOPush& push)
			{
				push.isFinalPass = 1u;
			});

		dispatchComputePass(
			frameCtx.cmdBuffer,
			Pipelines::getHandle(PipelineID::GTAODenoise),
			scope,
			frameCtx.descriptorWriter);
	}

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

	auto& depthResolved = ResourceManager::getDepthResolved_Target();

	auto& finalShadowMask = ResourceManager::getScreenSpaceShadowMask_Target();
	const auto pointSampler = ResourceManager::getPointBorder_Sampler();

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
			PUSH_BINDING_READ_1,
			depthResolved,
			pointSampler
		);
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_WRITE_1,
			finalShadowMask
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
	auto& depthResolved = ResourceManager::getDepthResolved_Target();
	auto& volumetricLight = ResourceManager::getVolumetricLight_Target();
	auto& volumetricBlur = ResourceManager::getVolumetricBlur_Target();

	const auto nearestClampSampler = ResourceManager::getNearestClamp_Sampler();
	const auto linearClampSampler = ResourceManager::getLinearClamp_Sampler();

	// === VOLUMETRIC LIGHT RAY MARCH ===

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		volumetricLight,
		VK_IMAGE_LAYOUT_GENERAL
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_READ_1,
		depthResolved,
		nearestClampSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_WRITE_1,
		volumetricLight
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
		PUSH_BINDING_READ_1,
		depthResolved,
		nearestClampSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_READ_2,
		volumetricLight,
		linearClampSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_WRITE_1,
		volumetricBlur
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
		PUSH_BINDING_READ_1,
		depthResolved,
		nearestClampSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_READ_2,
		volumetricBlur,
		linearClampSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_WRITE_1,
		volumetricLight
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
		opaque = ResourceManager::getColorHistoryWrite_Target();
	}
	else {
		opaque = ResourceManager::getOpaque_Target();
	}

	auto& transparent = ResourceManager::getTransparentResolved_Target();
	auto& dummy = ResourceManager::getDummy_Texture();
	const auto linearSampler = ResourceManager::getDefaultLinear_Sampler();

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_READ_1,
		opaque,
		linearSampler);

	if (transparentVisible) {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_READ_2,
			transparent,
			linearSampler);
	}
	else {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_READ_2,
			dummy,
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
		float cameraExposure;
		float adaptationSpeed;
		float deltaTime;
	} expPush{};
	expPush.totalTiles = totalTiles;
	expPush.cameraExposure = profiler.toneMappingSettings.cameraExposure;
	//expPush.adaptationSpeed = 0.5f;
	//expPush.deltaTime = profiler().getStats().deltaSecondsRaw;
	scope.setPush(expPush);

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
		opaque = ResourceManager::getColorHistoryWrite_Target();
	}
	else {
		opaque = ResourceManager::getOpaque_Target();
	}

	auto& transparent = ResourceManager::getTransparentResolved_Target();
	auto& dummy = ResourceManager::getDummy_Texture();
	auto& toneMap = ResourceManager::getToneMap_Target();

	const auto linearSampler = ResourceManager::getDefaultLinear_Sampler();
	auto& volLight = ResourceManager::getVolumetricLight_Target();
	const auto linearClampSampler = ResourceManager::getLinearClamp_Sampler();
	auto& lensFlareColor = ResourceManager::getLensFlareColor_Target();

	auto& cmaa2 = ResourceManager::getAAColor_Target();

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		toneMap,
		VK_IMAGE_LAYOUT_GENERAL);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_WRITE_1,
		toneMap);

	if (profiler.debugToggles.aaMode == AA_CMAA2) {
		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			cmaa2,
			VK_IMAGE_LAYOUT_GENERAL);

		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_WRITE_2,
			cmaa2);
	}
	else {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_WRITE_2,
			toneMap);
	}

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_READ_1,
		opaque,
		linearSampler);


	if (transparentVisible) {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_READ_2,
			transparent,
			linearSampler);
	}
	else {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_READ_2,
			dummy,
			linearSampler);
	}

	if (hasVisibles && profiler.debugToggles.enableVolumetrics && profiler.debugToggles.enableShadows) {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_READ_3,
			volLight,
			linearClampSampler);
	}
	else {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_READ_3,
			dummy,
			linearClampSampler);
	}

	if (hasVisibles && profiler.debugToggles.enableLensFlare) {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_READ_4,
			lensFlareColor,
			linearClampSampler);
	}
	else {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_READ_4,
			dummy,
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

	auto& aaColor = ResourceManager::getAAColor_Target();
	auto& tonemap = ResourceManager::getToneMap_Target();
	auto& finalComposite = ResourceManager::getPostNonAAComposite_Target();
	const auto linearClampSampler = ResourceManager::getLinearClamp_Sampler();

	if (hasVisibles && (profiler.debugToggles.aaMode != AA_OFF && profiler.debugToggles.aaMode != AA_TAA)) {
		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			aaColor,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		);

		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_READ_1,
			aaColor,
			linearClampSampler
		);
	}
	else {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_READ_1,
			tonemap,
			linearClampSampler
		);
	}

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		finalComposite,
		VK_IMAGE_LAYOUT_GENERAL
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_WRITE_1,
		finalComposite
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
		opaque = ResourceManager::getColorHistoryWrite_Target();
	}
	else {
		opaque = ResourceManager::getOpaque_Target();
	}
	auto& transparent = ResourceManager::getTransparentResolved_Target();
	auto& dummy = ResourceManager::getDummy_Texture();
	auto& flareBright = ResourceManager::getFlareBright_Target();
	auto& lensFlareColor = ResourceManager::getLensFlareColor_Target();
	const auto linearSampler = ResourceManager::getDefaultLinear_Sampler();
	auto& volLight = ResourceManager::getVolumetricLight_Target();
	const auto linearClampSampler = ResourceManager::getLinearClamp_Sampler();
	auto& hiZ = ResourceManager::getHiZ_Target();
	const auto hiZSampler = ResourceManager::getHiZ_Sampler();

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
		PUSH_BINDING_WRITE_1,
		flareBright);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_READ_1,
		opaque,
		linearSampler);

	if (transparentVisible) {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_READ_2,
			transparent,
			linearSampler);
	}
	else {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_READ_2,
			dummy,
			linearSampler);
	}

	if (hasVisibles && profiler.debugToggles.enableVolumetrics && profiler.debugToggles.enableShadows) {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_READ_3,
			volLight,
			linearClampSampler);
	}
	else {
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_READ_3,
			dummy,
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
		PUSH_BINDING_WRITE_1,
		lensFlareColor);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_READ_1,
		hiZ,
		hiZSampler);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_READ_2,
		flareBright,
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

	auto& hiZ = ResourceManager::getHiZ_Target();
	const auto hiZSampler = ResourceManager::getHiZ_Sampler();

	// === Tile slice ranges ===
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_READ_1,
		hiZ,
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
	auto& tonemap = ResourceManager::getToneMap_Target();
	auto& smaaColor = ResourceManager::getAAColor_Target();
	auto& smaaEdges = ResourceManager::getSMAAEdges_Target();
	auto& smaaWeights = ResourceManager::getSMAAWeights_Target();
	auto& depthResolved = ResourceManager::getDepthResolved_Target();
	const auto linearSampler = ResourceManager::getLinearLODClamp_Sampler();
	const auto nearestClampSampler = ResourceManager::getNearestClamp_Sampler();

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
		PUSH_BINDING_READ_1,
		tonemap,
		linearSampler
	);
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_READ_2,
		depthResolved,
		nearestClampSampler,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
	);
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_WRITE_1,
		smaaEdges
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
		PUSH_BINDING_READ_1,
		smaaEdges,
		linearSampler
	);
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_WRITE_1,
		smaaWeights
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
		PUSH_BINDING_READ_1,
		tonemap,
		linearSampler
	);
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_READ_2,
		smaaWeights,
		nearestClampSampler
	);
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_WRITE_1,
		smaaColor
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

	auto& cmaa2Color = ResourceManager::getAAColor_Target();
	auto& cmaa2WorkingEdges = ResourceManager::getCMAA2WorkingEdges_Target();
	auto& tonemap = ResourceManager::getToneMap_Target();
	const auto nearestClampSampler = ResourceManager::getNearestClamp_Sampler();
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
		PUSH_BINDING_READ_1,
		tonemap,
		nearestClampSampler
	);
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_WRITE_1,
		cmaa2WorkingEdges
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
		PUSH_BINDING_READ_1,
		tonemap,
		nearestClampSampler
	);
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_READ_2,
		cmaa2WorkingEdges,
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
		PUSH_BINDING_READ_1,
		tonemap,
		nearestClampSampler
	);
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_READ_2,
		cmaa2WorkingEdges,
		nearestClampSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_WRITE_1,
		cmaa2Color
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

	auto& opaqueColor = ResourceManager::getOpaque_Target();
	auto& colorHistoryRead = ResourceManager::getColorHistoryRead_Target();
	auto& colorHistoryWrite = ResourceManager::getColorHistoryWrite_Target();
	auto& depthResolved = ResourceManager::getDepthResolved_Target();
	auto& prevDepthResolved = ResourceManager::getPrevDepthResolved_Target();
	auto& velocity = ResourceManager::getVelocity_Target();
	auto& prevVelocity = ResourceManager::getPrevVelocity_Target();

	const auto nearestClampSampler = ResourceManager::getNearestClamp_Sampler();
	const auto taaSampler = ResourceManager::getTaaHistory_Sampler();

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
		PUSH_BINDING_READ_1,
		opaqueColor,
		nearestClampSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_READ_2,
		colorHistoryRead,
		taaSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_READ_3,
		velocity,
		nearestClampSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_READ_4,
		prevVelocity,
		taaSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_READ_5,
		depthResolved,
		nearestClampSampler,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
	);
	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_READ_6,
		prevDepthResolved,
		nearestClampSampler,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_WRITE_1,
		colorHistoryWrite
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

	auto& fxaaColor = ResourceManager::getAAColor_Target();

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		fxaaColor,
		VK_IMAGE_LAYOUT_GENERAL
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_READ_1,
		ResourceManager::getToneMap_Target(),
		ResourceManager::getLinearLODClamp_Sampler()
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_WRITE_1,
		fxaaColor
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

void RenderPasses::transparentResolvePass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.cmdBuffer,
		scope.passID
	);

	auto& transparentResolve = ResourceManager::getTransparentResolved_Target();
	auto& transparentAccum = ResourceManager::getTransparentAccumulation_Target();
	auto& transparentReveal = ResourceManager::getTransparentRevealage_Target();
	const auto nearestClampSampler = ResourceManager::getNearestClamp_Sampler();

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		transparentResolve,
		VK_IMAGE_LAYOUT_GENERAL);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_READ_1,
		transparentAccum,
		nearestClampSampler
	);

	frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_READ_2,
		transparentReveal,
		nearestClampSampler
	);

	 frameCtx.descriptorWriter.writePushImage(
		PUSH_BINDING_WRITE_1,
		transparentResolve
	);

	dispatchComputePass(
		frameCtx.cmdBuffer,
		Pipelines::getHandle(PipelineID::TransparentResolve),
		scope,
		frameCtx.descriptorWriter
	);

	 ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		transparentResolve,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
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
