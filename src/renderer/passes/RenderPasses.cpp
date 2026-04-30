#include "pch.h"

#include "RenderPasses.h"
#include "renderer/scene/Visibility.h"
#include "utils/BufferUtils.h"
#include "renderer/scene/RenderScene.h"
#include "engine/Engine.h"
#include "renderer/Renderer.h"


// TODO: REDESIGN ALL OF THIS

// Push constant functionality
template<typename PushType>
static constexpr void BindPushConstant(
	const PushType& push,
	VkCommandBuffer cmd)
{
	vkCmdPushConstants(
		cmd,
		layout.pipelineLayout,
		layout.pushContantRange.stageFlags,
		layout.pushConsantRange.offset,
		static_cast<uint32_t>(sizeof(PushType)),
		&push);
}


void RenderPasses::DirectionalCSMPass(
	FrameContext& frameCtx,
	GraphicsScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.m_commandBuffer,
		scope.passID
	);

	auto& shadowImg = ResourceManager::GetDirectionalCSMAtlas_Target();

	const VkExtent2D atlasExtent = { shadowImg.extent.width, shadowImg.extent.height };

	VkExtent2D tileExtent{};
	tileExtent.width = atlasExtent.width / 2u;
	tileExtent.height = atlasExtent.height / 2u;

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		shadowImg,
		VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	AttachmentDesc shadowDepth{};
	shadowDepth.imageView = shadowImg.imageView;
	shadowDepth.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	shadowDepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	shadowDepth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	shadowDepth.clearValue.depthStencil.depth = 1.0f;

	scope.bHasAtlas = true;
	scope.atlasExtent = tileExtent;

	auto& cascadeVP = RenderScene::getShadowCSM().cascadeVP;

	auto& pipeline = Pipelines::GetHandle(PipelineID::Shadow);

	for (uint32_t cascadeIdx = 0; cascadeIdx < MAX_SHADOW_CASCADES; ++cascadeIdx) {
		const uint32_t tileX = cascadeIdx % 2u;
		const uint32_t tileY = cascadeIdx / 2u;

		scope.atlasOffset.x = static_cast<int32_t>(tileX * tileExtent.width);
		scope.atlasOffset.y = static_cast<int32_t>(tileY * tileExtent.height);

		BeginRendering(
			frameCtx.m_commandBuffer,
			{ shadowDepth },
			tileExtent,
			scope);

		BindPushConstant(cascadeVP[cascadeIdx], frameCtx.m_commandBuffer);

		vkCmdBindPipeline(
			frameCtx.m_commandBuffer,
			pipeline.bindPoint,
			pipeline.pipeline);

		vkCmdDrawIndexedIndirect(
			frameCtx.m_commandBuffer,
			frameCtx.m_indirectDraws_GPU.m_buffer,
			frameCtx.m_shadowDrawRanges[cascadeIdx].firstCommand * DRAW_CMD_SIZE,
			frameCtx.m_shadowDrawRanges[cascadeIdx].commandCount,
			DRAW_CMD_SIZE);

		EndRendering(frameCtx.m_commandBuffer);

		if (profiler.debugToggles.enableProfilerView) {
			profiler.getStats().directionalCSMIndirect.commands += 1;
			profiler.getStats().directionalCSMIndirect.subdraws += frameCtx.m_shadowDrawRanges[cascadeIdx].firstCommand * DRAW_CMD_SIZE;
		}
	}

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		shadowImg,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
}

void RenderPasses::ShadowFlashlightPass(
	FrameContext& frameCtx,
	GraphicsScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.m_commandBuffer,
		scope.passID
	);

	auto& flashlightShadow = ResourceManager::GetFlashlightShadowMap_Target();

	const VkExtent2D extent = { flashlightShadow.extent.width, flashlightShadow.extent.height };

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		flashlightShadow,
		VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	AttachmentDesc shadowDepth{};
	shadowDepth.imageView = flashlightShadow.imageView;
	shadowDepth.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	shadowDepth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	shadowDepth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	shadowDepth.clearValue.depthStencil.depth = 1.0f;

	BeginRendering(
		frameCtx.m_commandBuffer,
		{ shadowDepth },
		extent,
		scope);

	BindPushConstant(LightingSystem::_mainFlashLight.viewProj, frameCtx.m_commandBuffer);

	auto& pipeline = Pipelines::GetHandle(PipelineID::Shadow);

	vkCmdBindPipeline(
		frameCtx.m_commandBuffer,
		pipeline.bindPoint,
		pipeline.pipeline);

	vkCmdDrawIndexedIndirect(
		frameCtx.m_commandBuffer,
		frameCtx.m_indirectDraws_GPU.m_buffer,
		frameCtx.m_flashlightShadowCasterRange.firstCommand * DRAW_CMD_SIZE,
		frameCtx.m_flashlightShadowCasterRange.commandCount,
		DRAW_CMD_SIZE);

	EndRendering(frameCtx.m_commandBuffer);

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		flashlightShadow,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);

	if (profiler.debugToggles.enableProfilerView) {
		profiler.getStats().flashlightShadowIndirect.commands += 1;
		profiler.getStats().flashlightShadowIndirect.subdraws += frameCtx.m_flashlightShadowCasterRange.firstCommand * DRAW_CMD_SIZE;
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
		frameCtx.m_commandBuffer,
		scope.passID
	);

	auto& depthResolved = ResourceManager::GetDepthResolved_Target();
	auto& prevDepthResolved = ResourceManager::GetPrevDepthResolved_Target();
	auto& velocity = ResourceManager::GetVelocity_Target();
	auto& prevVelocity = ResourceManager::GetPrevVelocity_Target();
	auto& viewSpaceNormals = ResourceManager::GetViewSpaceNormals_Target();

	if (isTemporalValid) {
		ImageUtils::imageCopy(
			frameCtx.m_commandBuffer,
			depthResolved,
			prevDepthResolved,
			VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);
		ImageUtils::imageCopy(
			frameCtx.m_commandBuffer,
			velocity,
			prevVelocity,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}
	else {
		ImageUtils::transitionImage(
			frameCtx.m_commandBuffer,
			depthResolved,
			VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

		ImageUtils::transitionImage(
			frameCtx.m_commandBuffer,
			velocity,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	}

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		viewSpaceNormals,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

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

	BeginRendering(
		frameCtx.m_commandBuffer,
		{ prepassNormal, prepassVelocity, prepassDepth },
		{ depthResolved.extent.width, depthResolved.extent.height },
		scope);

	auto& pipeline = Pipelines::GetHandle(PipelineID::Prepass);

	vkCmdBindPipeline(
		frameCtx.m_commandBuffer,
		pipeline.bindPoint,
		pipeline.pipeline);

	vkCmdDrawIndexedIndirect(frameCtx.m_commandBuffer,
		frameCtx.m_indirectDraws_GPU.m_buffer,
		frameCtx.m_opaqueDrawRange.firstCommand * DRAW_CMD_SIZE,
		frameCtx.m_opaqueDrawRange.commandCount,
		DRAW_CMD_SIZE);

	EndRendering(frameCtx.m_commandBuffer);

	// Transition images to be sampled
	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		depthResolved,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL);

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		velocity,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		viewSpaceNormals,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void RenderPasses::HiZGenerationPass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.m_commandBuffer,
		scope.passID
	);

	auto& depthResolved = ResourceManager::GetDepthResolved_Target();
	auto& dummyUint8 = ResourceManager::GetDummyUint8_Texture();
	auto nearestClampSampler = ResourceManager::GetNearestClamp_Sampler();
	auto& hiZ = ResourceManager::GetHiZ_Target();
	auto hiZSampler = ResourceManager::GetHiZ_Sampler();

	// Transition all mips to GENERAL for compute writes
	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		hiZ,
		VK_IMAGE_LAYOUT_GENERAL,
		0u,                        // Start at base mip
		hiZ.mipLevelCount          // All levels transitioned
	);

	// First ever transition
	if (dummyUint8.currentLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
		ImageUtils::transitionImage(
			frameCtx.m_commandBuffer,
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

	scope.SetPush(push);

	VkExtent3D srcExtent = hiZ.extent; // Start at full res
	VkExtent3D dstExtent = hiZ.extent; // updated after each iteration

	for (uint32_t mip = 0; mip < hiZ.mipLevelCount; ++mip) {
		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_READ_1,
			depthResolved,
			nearestClampSampler);

		if (mip > 0) {
			frameCtx.m_descriptorWriter.WritePushImage(
				PUSH_BINDING_READ_2,
				hiZ,
				hiZSampler,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				(mip - 1u));
		}
		else {
			// Empty image for first copy stage at mip 0
			frameCtx.m_descriptorWriter.WritePushImage(
				PUSH_BINDING_READ_2,
				dummyUint8,
				hiZSampler);
		}
		frameCtx.m_descriptorWriter.WritePushImage(
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

		DispatchComputePass(
			frameCtx.m_commandBuffer,
			Pipelines::GetHandle(PipelineID::HiZGen),
			scope,
			frameCtx.m_descriptorWriter);

		ImageUtils::transitionImage(
			frameCtx.m_commandBuffer,
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

void RenderPasses::SSAOPass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler,
	const bool isTemporalValid)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.m_commandBuffer,
		scope.passID
	);
	auto& depthResolved = ResourceManager::GetDepthResolved_Target();
	auto& linearizedMinHiZ = ResourceManager::GetLinearizedMinHiZ_Target();
	auto& rawAO = ResourceManager::GetAORaw_Target();
	auto& aoTemp = ResourceManager::GetAOTemp_Target();
	auto& edgeInfo = ResourceManager::GetAOEdgeInfo_Target();
	auto& bentNormals = ResourceManager::GetBentNormals_Target();
	auto& viewSpaceNormals = ResourceManager::GetViewSpaceNormals_Target();

	auto nearestClampSampler = ResourceManager::GetNearestClamp_Sampler();
	auto depthPyramidSampler = ResourceManager::GetHiZ_Sampler();
	auto aoSampler = ResourceManager::GetLinearLODClamp_Sampler();
	auto nearSampler = ResourceManager::GetDefaultNearest_Sampler();

	// ============================
	// === SSAO DEPTH PREFILTER ===
	{
		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_READ_1,
			depthResolved,
			nearestClampSampler);

		ImageUtils::transitionImage(
			frameCtx.m_commandBuffer,
			linearizedMinHiZ,
			VK_IMAGE_LAYOUT_GENERAL,
			0u,
			linearizedMinHiZ.mipLevelCount);

		uint32_t pushWriteBinding = PUSH_BINDING_WRITE_1;
		for (uint32_t i = 0u; i < HI_Z_MIP_COUNT; i++) {
			ASSERT(pushWriteBinding <= PUSH_BINDING_WRITE_5);

			frameCtx.m_descriptorWriter.WritePushImage(
				pushWriteBinding,
				linearizedMinHiZ,
				VK_NULL_HANDLE,
				VK_IMAGE_LAYOUT_GENERAL,
				i);
			pushWriteBinding++;
		}

		DispatchComputePass(
			frameCtx.m_commandBuffer,
			Pipelines::GetHandle(PipelineID::SSAODepthPrefilter),
			scope,
			frameCtx.m_descriptorWriter);

		ImageUtils::transitionImage(
			frameCtx.m_commandBuffer,
			linearizedMinHiZ,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			0u,
			linearizedMinHiZ.mipLevelCount);
	}

	// =================
	// === SSAO MAIN ===

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		rawAO,
		VK_IMAGE_LAYOUT_GENERAL);
	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		edgeInfo,
		VK_IMAGE_LAYOUT_GENERAL);

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		bentNormals,
		VK_IMAGE_LAYOUT_GENERAL);

	// Inputs
	// Depth pyramid
	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_1,
		linearizedMinHiZ,
		depthPyramidSampler);
	// View space normals
	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_2,
		viewSpaceNormals,
		nearestClampSampler);

	// Outputs
	// raw ao
	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_WRITE_1,
		rawAO);
	// edge info
	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_WRITE_2,
		edgeInfo);
	// bent normals
	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_WRITE_3,
		bentNormals);

	DispatchComputePass(
		frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::SSAO),
		scope,
		frameCtx.m_descriptorWriter);

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		bentNormals,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

 
	// Spatial filtering / Denoising initial setup

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		rawAO,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		edgeInfo,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		aoTemp,
		VK_IMAGE_LAYOUT_GENERAL);

	// Inputs
	// ao
	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_1,
		rawAO,
		aoSampler
	);
	// edge info
	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_2,
		edgeInfo,
		nearSampler
	);

	// Output
	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_WRITE_1,
		aoTemp
	);

	// Bi-lateral blur filter
	if (profiler.debugToggles.aaMode != AA_TAA) {
		// ==============================
		// === SSAO FILTER HORIZONTAL ===

		DispatchComputePass(
			frameCtx.m_commandBuffer,
			Pipelines::GetHandle(PipelineID::SSAOFilter),
			scope,
			frameCtx.m_descriptorWriter);

		// ============================
		// === SSAO FILTER VERTICAL ===

		ImageUtils::transitionImage(
			frameCtx.m_commandBuffer,
			aoTemp,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		ImageUtils::transitionImage(
			frameCtx.m_commandBuffer,
			rawAO,
			VK_IMAGE_LAYOUT_GENERAL);


		// Inputs
		// Filtered Horionzal ao
		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_READ_1,
			aoTemp,
			aoSampler
		);
		// edge info
		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_READ_2,
			edgeInfo,
			nearSampler
		);

		// Output
		// final filtered ao
		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_WRITE_1,
			rawAO
		);

		scope.EditPush<SSAOPush>(
			[](SSAOPush& push)
			{
				push.blurDirection = { 0.0f, 1.0f }; // Vertical
			});
		DispatchComputePass(
			frameCtx.m_commandBuffer,
			Pipelines::GetHandle(PipelineID::SSAOFilter),
			scope,
			frameCtx.m_descriptorWriter);
	}
	// XeGTAO Denoise (TAA required)
	else {
		// ===========================
		// === SSAO DENOISE PASS 1 ===

		// Works over 2x1 pixels
		scope.workgroupSize = { 32u, 16u, 1u };

		DispatchComputePass(
			frameCtx.m_commandBuffer,
			Pipelines::GetHandle(PipelineID::SSAODenoise),
			scope,
			frameCtx.m_descriptorWriter);

		ImageUtils::transitionImage(
			frameCtx.m_commandBuffer,
			aoTemp,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		ImageUtils::transitionImage(
			frameCtx.m_commandBuffer,
			rawAO,
			VK_IMAGE_LAYOUT_GENERAL);


		// ===========================
		// === SSAO DENOISE PASS 2 ===

		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_READ_1,
			aoTemp,
			aoSampler
		);
		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_READ_2,
			edgeInfo,
			nearSampler
		);

		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_WRITE_1,
			rawAO
		);

		scope.EditPush<SSAOPush>(
			[](SSAOPush& push)
			{
				push.isFinalPass = 1u;
			});

		DispatchComputePass(
			frameCtx.m_commandBuffer,
			Pipelines::GetHandle(PipelineID::SSAODenoise),
			scope,
			frameCtx.m_descriptorWriter);
	}

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		rawAO,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);
}

void RenderPasses::SSContactShadowsPass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.m_commandBuffer,
		scope.passID
	);

	auto& depthResolved = ResourceManager::GetDepthResolved_Target();

	auto& finalShadowMask = ResourceManager::GetScreenSpaceShadowMask_Target();
	const auto pointSampler = ResourceManager::GetPointBorder_Sampler();

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		finalShadowMask,
		VK_IMAGE_LAYOUT_GENERAL
	);

	const auto& list = RenderScene::_dispatchListSSS;

	const auto& pixelSizes = RenderScene::getCurrentSceneData().pixelSizes;
	glm::vec2 invSize = glm::vec2(pixelSizes.x, pixelSizes.y);

	scope.EditPush<SSSPush>(
		[invSize, list](SSSPush& push)
		{
			push.lightCoords = list.lightCoords;
			push.invDepthSize = invSize;
		});

	for (int i = 0; i < list.dispatchCount; i++) {
		const DispatchData& disp = list.dispatch[i];

		scope.EditPush<SSSPush>(
			[disp](SSSPush& push)
			{
				push.waveOffsets = disp.waveOffset;
			});

		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_READ_1,
			depthResolved,
			pointSampler
		);
		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_WRITE_1,
			finalShadowMask
		);

		scope.groupCountX = disp.waveCount[0];
		scope.groupCountY = disp.waveCount[1];
		scope.groupCountZ = disp.waveCount[2];

		DispatchComputePass(
			frameCtx.m_commandBuffer,
			Pipelines::GetHandle(PipelineID::ScreenSpaceContactShadows),
			scope,
			frameCtx.m_descriptorWriter
		);
	}

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		finalShadowMask,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);
}

void RenderPasses::VolumetricLightingPass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.m_commandBuffer,
		scope.passID
	);
	auto& depthResolved = ResourceManager::GetDepthResolved_Target();
	auto& volumetricLight = ResourceManager::GetVolumetricLight_Target();
	auto& volumetricBlur = ResourceManager::GetVolumetricBlur_Target();

	const auto nearestClampSampler = ResourceManager::GetNearestClamp_Sampler();
	const auto linearClampSampler = ResourceManager::GetLinearClamp_Sampler();

	// === VOLUMETRIC LIGHT RAY MARCH ===

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		volumetricLight,
		VK_IMAGE_LAYOUT_GENERAL
	);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_1,
		depthResolved,
		nearestClampSampler
	);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_WRITE_1,
		volumetricLight
	);

	DispatchComputePass(
		frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::VolumetricLight),
		scope,
		frameCtx.m_descriptorWriter);

	// === BLUR HORIZONTAL ===

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		volumetricLight,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		volumetricBlur,
		VK_IMAGE_LAYOUT_GENERAL
	);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_1,
		depthResolved,
		nearestClampSampler
	);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_2,
		volumetricLight,
		linearClampSampler
	);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_WRITE_1,
		volumetricBlur
	);

	scope.EditPush<VolumetricPush>(
		[](VolumetricPush& push)
		{
			push.blurDirection = { 1.0f, 0.0f }; // Horizontal
		});

	DispatchComputePass(
		frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::VolumetricLightBlur),
		scope,
		frameCtx.m_descriptorWriter);


	// === VERTICAL BLUR ===

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		volumetricLight,
		VK_IMAGE_LAYOUT_GENERAL
	);

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		volumetricBlur,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_1,
		depthResolved,
		nearestClampSampler
	);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_2,
		volumetricBlur,
		linearClampSampler
	);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_WRITE_1,
		volumetricLight
	);

	scope.EditPush<VolumetricPush>(
		[](VolumetricPush& push)
		{
			push.blurDirection = { 0.0f, 1.0f }; // Vertical
		});

	DispatchComputePass(
		frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::VolumetricLightBlur),
		scope,
		frameCtx.m_descriptorWriter);

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		volumetricLight,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);
}

void RenderPasses::ExposurePass(
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
		frameCtx.m_commandBuffer,
		scope.passID
	);

	AllocatedImage opaque{};
	if (profiler.debugToggles.aaMode == AA_TAA && isTemporalValid && hasVisibles) {
		opaque = ResourceManager::GetColorHistoryWrite_Target();
	}
	else {
		opaque = ResourceManager::GetOpaque_Target();
	}

	auto& transparent = ResourceManager::GetTransparentResolved_Target();
	auto& dummy = ResourceManager::GetDummy_Texture();
	const auto linearSampler = ResourceManager::GetDefaultLinear_Sampler();

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_1,
		opaque,
		linearSampler);

	if (transparentVisible) {
		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_READ_2,
			transparent,
			linearSampler);
	}
	else {
		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_READ_2,
			dummy,
			linearSampler);
	}

	// === EXPOSURE REDUCE ===
	DispatchComputePass(
		frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::ExposureReduce),
		scope,
		frameCtx.m_descriptorWriter);

	BarrierUtils::BufferComputeWriteToComputeRead(frameCtx.m_commandBuffer, luminanceBuf);

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
	scope.SetPush(expPush);

	DispatchComputePass(
		frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::ExposureFinalize),
		scope,
		frameCtx.m_descriptorWriter);

	BarrierUtils::BufferComputeWriteToComputeRead(frameCtx.m_commandBuffer, luminanceBuf);
}

void RenderPasses::FinalCompositePass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler,
	const bool transparentVisible,
	const bool hasVisibles,
	const bool isTemporalValid)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.m_commandBuffer,
		scope.passID
	);

	AllocatedImage opaque{};
	if (profiler.debugToggles.aaMode == AA_TAA && isTemporalValid && hasVisibles) {
		opaque = ResourceManager::GetColorHistoryWrite_Target();
	}
	else {
		opaque = ResourceManager::GetOpaque_Target();
	}

	auto& transparent = ResourceManager::GetTransparentResolved_Target();
	auto& dummy = ResourceManager::GetDummy_Texture();
	auto& toneMap = ResourceManager::GetToneMap_Target();

	const auto linearSampler = ResourceManager::GetDefaultLinear_Sampler();
	auto& volLight = ResourceManager::GetVolumetricLight_Target();
	const auto linearClampSampler = ResourceManager::GetLinearClamp_Sampler();
	auto& lensFlareColor = ResourceManager::GetLensFlareColor_Target();

	auto& cmaa2 = ResourceManager::GetAAColor_Target();

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		toneMap,
		VK_IMAGE_LAYOUT_GENERAL);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_WRITE_1,
		toneMap);

	if (profiler.debugToggles.aaMode == AA_CMAA2) {
		ImageUtils::transitionImage(
			frameCtx.m_commandBuffer,
			cmaa2,
			VK_IMAGE_LAYOUT_GENERAL);

		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_WRITE_2,
			cmaa2);
	}
	else {
		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_WRITE_2,
			toneMap);
	}

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_1,
		opaque,
		linearSampler);


	if (transparentVisible) {
		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_READ_2,
			transparent,
			linearSampler);
	}
	else {
		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_READ_2,
			dummy,
			linearSampler);
	}

	if (hasVisibles && profiler.debugToggles.enableVolumetrics && profiler.debugToggles.enableShadows) {
		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_READ_3,
			volLight,
			linearClampSampler);
	}
	else {
		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_READ_3,
			dummy,
			linearClampSampler);
	}

	if (hasVisibles && profiler.debugToggles.enableLensFlare) {
		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_READ_4,
			lensFlareColor,
			linearClampSampler);
	}
	else {
		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_READ_4,
			dummy,
			linearClampSampler);
	}

	DispatchComputePass(
		frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::FinalComposite),
		scope,
		frameCtx.m_descriptorWriter);

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		toneMap,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void RenderPasses::ChromaticAberrationPass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler,
	const bool hasVisibles)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.m_commandBuffer,
		scope.passID
	);

	auto& aaColor = ResourceManager::GetAAColor_Target();
	auto& tonemap = ResourceManager::GetToneMap_Target();
	auto& finalComposite = ResourceManager::GetPostNonAAComposite_Target();
	const auto linearClampSampler = ResourceManager::GetLinearClamp_Sampler();

	if (hasVisibles && (profiler.debugToggles.aaMode != AA_OFF && profiler.debugToggles.aaMode != AA_TAA)) {
		ImageUtils::transitionImage(
			frameCtx.m_commandBuffer,
			aaColor,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
		);

		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_READ_1,
			aaColor,
			linearClampSampler
		);
	}
	else {
		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_READ_1,
			tonemap,
			linearClampSampler
		);
	}

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		finalComposite,
		VK_IMAGE_LAYOUT_GENERAL
	);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_WRITE_1,
		finalComposite
	);

	DispatchComputePass(
		frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::ChromaticAberration),
		scope,
		frameCtx.m_descriptorWriter
	);
}

void RenderPasses::LensFlarePass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler,
	const bool transparentVisible,
	const bool hasVisibles,
	const bool isTemporalValid)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.m_commandBuffer,
		scope.passID
	);
	AllocatedImage opaque{};
	if (profiler.debugToggles.aaMode == AA_TAA && isTemporalValid && hasVisibles) {
		opaque = ResourceManager::GetColorHistoryWrite_Target();
	}
	else {
		opaque = ResourceManager::GetOpaque_Target();
	}
	auto& transparent = ResourceManager::GetTransparentResolved_Target();
	auto& dummy = ResourceManager::GetDummy_Texture();
	auto& flareBright = ResourceManager::GetFlareBright_Target();
	auto& lensFlareColor = ResourceManager::GetLensFlareColor_Target();
	const auto linearSampler = ResourceManager::GetDefaultLinear_Sampler();
	auto& volLight = ResourceManager::GetVolumetricLight_Target();
	const auto linearClampSampler = ResourceManager::GetLinearClamp_Sampler();
	auto& hiZ = ResourceManager::GetHiZ_Target();
	const auto hiZSampler = ResourceManager::GetHiZ_Sampler();

	// Get both lens flare outputs ready
	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		flareBright,
		VK_IMAGE_LAYOUT_GENERAL);

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		lensFlareColor,
		VK_IMAGE_LAYOUT_GENERAL);

	// === FLARE BRIGHT STAGE ===
	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_WRITE_1,
		flareBright);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_1,
		opaque,
		linearSampler);

	if (transparentVisible) {
		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_READ_2,
			transparent,
			linearSampler);
	}
	else {
		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_READ_2,
			dummy,
			linearSampler);
	}

	if (hasVisibles && profiler.debugToggles.enableVolumetrics && profiler.debugToggles.enableShadows) {
		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_READ_3,
			volLight,
			linearClampSampler);
	}
	else {
		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_READ_3,
			dummy,
			linearClampSampler);
	}

	DispatchComputePass(
		frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::FlareBright),
		scope,
		frameCtx.m_descriptorWriter);

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		flareBright,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);


	// === LENS FLARE COLOR STAGE ===
	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_WRITE_1,
		lensFlareColor);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_1,
		hiZ,
		hiZSampler);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_2,
		flareBright,
		linearClampSampler);

	DispatchComputePass(
		frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::FlareGen),
		scope,
		frameCtx.m_descriptorWriter);

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		lensFlareColor,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void RenderPasses::ClusterLightCullingPass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.m_commandBuffer,
		scope.passID
	);
	// Reset buffers
	vkCmdFillBuffer(
		frameCtx.m_commandBuffer,
		frameCtx.m_visibleLightCount_GPU.m_buffer,
		0u,
		frameCtx.m_visibleLightCount_GPU.m_allocInfo.size,
		0u
	);
	vkCmdFillBuffer(
		frameCtx.m_commandBuffer,
		frameCtx.m_clusterCounts_GPU.m_buffer,
		0u,
		frameCtx.m_clusterCounts_GPU.m_allocInfo.size,
		0u
	);
	vkCmdFillBuffer(
		frameCtx.m_commandBuffer,
		frameCtx.m_clusterCursors_GPU.m_buffer,
		0u,
		frameCtx.m_clusterCursors_GPU.m_allocInfo.size,
		0u
	);

	vkCmdFillBuffer(
		frameCtx.m_commandBuffer,
		frameCtx.m_clusterScanScratch_GPU.m_buffer,
		0u,
		frameCtx.m_clusterScanScratch_GPU.m_allocInfo.size,
		0u
	);

	VkDeviceSize dispatchLightOffsetBytes = INDIRECT_DISPATCH_SLOT_LIGHTS * DISPATCH_SLOT_STRIDE_BYTES;
	VkDeviceSize dispatchClusterOffsetBytes = INDIRECT_DISPATCH_SLOT_CLUSTERS * DISPATCH_SLOT_STRIDE_BYTES;

	vkCmdFillBuffer(
		frameCtx.m_commandBuffer,
		frameCtx.m_dispatchIndirectArgs_GPU.m_buffer,
		dispatchLightOffsetBytes,
		DISPATCH_SLOT_STRIDE_BYTES + DISPATCH_SLOT_STRIDE_BYTES,
		0u
	);

	BarrierUtils::BufferFillToComputeRW(frameCtx.m_commandBuffer, frameCtx.m_visibleLightCount_GPU);
	BarrierUtils::BufferFillToComputeRW(frameCtx.m_commandBuffer, frameCtx.m_clusterCounts_GPU);
	BarrierUtils::BufferFillToComputeRW(frameCtx.m_commandBuffer, frameCtx.m_clusterCursors_GPU);
	BarrierUtils::BufferFillToComputeRW(frameCtx.m_commandBuffer, frameCtx.m_clusterScanScratch_GPU);
	BarrierUtils::BufferFillToComputeRW(frameCtx.m_commandBuffer, frameCtx.m_dispatchIndirectArgs_GPU);

	auto& hiZ = ResourceManager::GetHiZ_Target();
	const auto hiZSampler = ResourceManager::GetHiZ_Sampler();

	// === Tile slice ranges ===
	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_1,
		hiZ,
		hiZSampler);

	DispatchComputePass(
		frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::ClusterTileSliceRanges),
		scope,
		frameCtx.m_descriptorWriter);

	BarrierUtils::BufferComputeWriteToComputeRead(frameCtx.m_commandBuffer, frameCtx.m_clusterTileSliceRanges_GPU);

	scope.workgroupSize = { 256u, 1u, 1u };

	auto activeLightCount = LightingSystem::GetActiveLightCount();

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

	scope.SetPush(pc);

	// === Visible light list ===
	DispatchComputePass(
		frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::VisibleLightList),
		scope,
		frameCtx.m_descriptorWriter);

	BarrierUtils::BufferComputeWriteToComputeRead(frameCtx.m_commandBuffer, frameCtx.m_visibleLightCount_GPU);
	BarrierUtils::BufferComputeWriteToComputeRead(frameCtx.m_commandBuffer, frameCtx.m_visibleLightIDs_GPU);

	scope.ClearPush();

	scope.extent = { 1u, 1u };
	scope.workgroupSize = { 1u, 1u, 1u };

	// === Dispatch args compute for visible lights/clusters
	DispatchComputePass(
		frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::IndirectArgsLight),
		scope,
		frameCtx.m_descriptorWriter
	);

	BarrierUtils::BufferComputeWriteToIndirectDispatchRead(frameCtx.m_commandBuffer, frameCtx.m_dispatchIndirectArgs_GPU);

	// Last passes have data on visible lights and clusters
	scope.SetIndirect(frameCtx.m_dispatchIndirectArgs_GPU.m_buffer, dispatchLightOffsetBytes);

	// === Cluster counts ===
	DispatchComputePass(
		frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::ClusterCount),
		scope,
		frameCtx.m_descriptorWriter);

	BarrierUtils::BufferComputeWriteToComputeRead(frameCtx.m_commandBuffer, frameCtx.m_clusterCounts_GPU);

	scope.SetIndirect(frameCtx.m_dispatchIndirectArgs_GPU.m_buffer, dispatchClusterOffsetBytes);
	// === scan offsets ===
	DispatchComputePass(
		frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::ClusterScanOffsets),
		scope,
		frameCtx.m_descriptorWriter);

	BarrierUtils::BufferComputeWriteToComputeRead(frameCtx.m_commandBuffer, frameCtx.m_clusterOffsets_GPU);
	BarrierUtils::BufferComputeWriteToComputeRead(frameCtx.m_commandBuffer, frameCtx.m_clusterScanScratch_GPU);

	scope.SetIndirect(frameCtx.m_dispatchIndirectArgs_GPU.m_buffer, dispatchLightOffsetBytes);
	// === scatter ids ===
	DispatchComputePass(
		frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::ClusterScatterIDs),
		scope,
		frameCtx.m_descriptorWriter);

	BarrierUtils::BufferComputeWriteToFragmentRead(frameCtx.m_commandBuffer, frameCtx.m_clusterLightIDs_GPU);
}

void RenderPasses::SMAAPass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.m_commandBuffer,
		scope.passID
	);
	auto& tonemap = ResourceManager::GetToneMap_Target();
	auto& smaaColor = ResourceManager::GetAAColor_Target();
	auto& smaaEdges = ResourceManager::GetSMAAEdges_Target();
	auto& smaaWeights = ResourceManager::GetSMAAWeights_Target();
	auto& depthResolved = ResourceManager::GetDepthResolved_Target();
	const auto linearSampler = ResourceManager::GetLinearLODClamp_Sampler();
	const auto nearestClampSampler = ResourceManager::GetNearestClamp_Sampler();

	// Push constant only required for weight blending
	scope.bSkipPushConstant = true;

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		smaaColor,
		VK_IMAGE_LAYOUT_GENERAL
	);
	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		smaaEdges,
		VK_IMAGE_LAYOUT_GENERAL
	);
	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		smaaWeights,
		VK_IMAGE_LAYOUT_GENERAL
	);


	// SMAA STAGE 1 EDGE CALCULATION
	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_1,
		tonemap,
		linearSampler
	);
	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_2,
		depthResolved,
		nearestClampSampler,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
	);
	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_WRITE_1,
		smaaEdges
	);

	DispatchComputePass(frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::SMAAEdges),
		scope,
		frameCtx.m_descriptorWriter);

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		smaaEdges,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);

	scope.bSkipPushConstant = false;

	// SMAA STAGE 2 WEIGHT BLENDING
	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_1,
		smaaEdges,
		linearSampler
	);
	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_WRITE_1,
		smaaWeights
	);

	DispatchComputePass(frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::SMAAWeights),
		scope,
		frameCtx.m_descriptorWriter);

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		smaaWeights,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);

	scope.bSkipPushConstant = true;

	// SMAA STAGE 3 NEIGHBOURHOOD BLENDING
	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_1,
		tonemap,
		linearSampler
	);
	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_2,
		smaaWeights,
		nearestClampSampler
	);
	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_WRITE_1,
		smaaColor
	);

	DispatchComputePass(frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::SMAABlend),
		scope,
		frameCtx.m_descriptorWriter);
}

void RenderPasses::CMAA2Pass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.m_commandBuffer,
		scope.passID
	);

	auto& cmaa2Color = ResourceManager::GetAAColor_Target();
	auto& cmaa2WorkingEdges = ResourceManager::GetCMAA2WorkingEdges_Target();
	auto& tonemap = ResourceManager::GetToneMap_Target();
	const auto nearestClampSampler = ResourceManager::GetNearestClamp_Sampler();
	VkDeviceSize processOffsetBytes = INDIRECT_DISPATCH_SLOT_CMAA2_SHAPES * DISPATCH_SLOT_STRIDE_BYTES;
	VkDeviceSize deferredOffsetBytes = INDIRECT_DISPATCH_SLOT_CMAA2_DEFERRED * DISPATCH_SLOT_STRIDE_BYTES;

	// Reset buffers
	{
		vkCmdFillBuffer(
			frameCtx.m_commandBuffer,
			frameCtx.m_cmaa2Control_GPU.m_buffer,
			0u,
			frameCtx.m_cmaa2Control_GPU.m_allocInfo.size,
			0u
		);
		vkCmdFillBuffer(
			frameCtx.m_commandBuffer,
			frameCtx.m_cmaa2DeferredHeads_GPU.m_buffer,
			0u,
			frameCtx.m_cmaa2DeferredHeads_GPU.m_allocInfo.size,
			0x7FFFFFFFu
		);

		BarrierUtils::BufferFillToComputeRW(frameCtx.m_commandBuffer, frameCtx.m_cmaa2Control_GPU);
		BarrierUtils::BufferFillToComputeRW(frameCtx.m_commandBuffer, frameCtx.m_cmaa2DeferredHeads_GPU);
	}

	// BUILD EDGES
	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		cmaa2WorkingEdges,
		VK_IMAGE_LAYOUT_GENERAL
	);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_1,
		tonemap,
		nearestClampSampler
	);
	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_WRITE_1,
		cmaa2WorkingEdges
	);

	DispatchComputePass(frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::CMAA2Edges),
		scope,
		frameCtx.m_descriptorWriter);

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		cmaa2WorkingEdges,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);

	BarrierUtils::BufferComputeWriteToComputeRead(frameCtx.m_commandBuffer, frameCtx.m_cmaa2Control_GPU);
	BarrierUtils::BufferComputeWriteToComputeRead(frameCtx.m_commandBuffer, frameCtx.m_cmaa2ShapeCandidates_GPU);


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

	scope.SetPush(argsPush);

	DispatchComputePass(frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::CMAA2DispatchArgs),
		scope,
		frameCtx.m_descriptorWriter);

	BarrierUtils::BufferComputeWriteToIndirectDispatchRead(frameCtx.m_commandBuffer, frameCtx.m_dispatchIndirectArgs_GPU);


	// PROCESS CANDIDATES

	scope.SetPush(frameCtx.m_cmaa2Push);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_1,
		tonemap,
		nearestClampSampler
	);
	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_2,
		cmaa2WorkingEdges,
		nearestClampSampler
	);

	scope.SetIndirect(frameCtx.m_dispatchIndirectArgs_GPU.m_buffer, processOffsetBytes);

	DispatchComputePass(frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::CMAA2ShapeCandidates),
		scope,
		frameCtx.m_descriptorWriter);

	BarrierUtils::BufferComputeWriteToComputeRead(frameCtx.m_commandBuffer, frameCtx.m_cmaa2Control_GPU);
	BarrierUtils::BufferComputeWriteToComputeRead(frameCtx.m_commandBuffer, frameCtx.m_cmaa2DeferredLocations_GPU);
	BarrierUtils::BufferComputeWriteToComputeRead(frameCtx.m_commandBuffer, frameCtx.m_cmaa2DeferredItems_GPU);
	BarrierUtils::BufferComputeWriteToComputeRead(frameCtx.m_commandBuffer, frameCtx.m_cmaa2DeferredHeads_GPU);

	scope.ClearIndirect();

	// COMPUTE DISPATCH ARGS 2
	argsPush.pass = 1;
	scope.SetPush(argsPush);

	DispatchComputePass(frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::CMAA2DispatchArgs),
		scope,
		frameCtx.m_descriptorWriter);

	BarrierUtils::BufferComputeWriteToIndirectDispatchRead(frameCtx.m_commandBuffer, frameCtx.m_dispatchIndirectArgs_GPU);

	// DEFERRED COLOR APPLY
	scope.SetPush(frameCtx.m_cmaa2Push);
	scope.SetIndirect(frameCtx.m_dispatchIndirectArgs_GPU.m_buffer, deferredOffsetBytes);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_1,
		tonemap,
		nearestClampSampler
	);
	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_2,
		cmaa2WorkingEdges,
		nearestClampSampler
	);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_WRITE_1,
		cmaa2Color
	);

	DispatchComputePass(frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::CMAA2DeferredResolve),
		scope,
		frameCtx.m_descriptorWriter);
}

void RenderPasses::TAAPass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.m_commandBuffer,
		scope.passID
	);

	auto& opaqueColor = ResourceManager::GetOpaque_Target();
	auto& colorHistoryRead = ResourceManager::GetColorHistoryRead_Target();
	auto& colorHistoryWrite = ResourceManager::GetColorHistoryWrite_Target();
	auto& depthResolved = ResourceManager::GetDepthResolved_Target();
	auto& prevDepthResolved = ResourceManager::GetPrevDepthResolved_Target();
	auto& velocity = ResourceManager::GetVelocity_Target();
	auto& prevVelocity = ResourceManager::GetPrevVelocity_Target();

	const auto nearestClampSampler = ResourceManager::GetNearestClamp_Sampler();
	const auto taaSampler = ResourceManager::GetTaaHistory_Sampler();

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		colorHistoryRead,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		colorHistoryWrite,
		VK_IMAGE_LAYOUT_GENERAL
	);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_1,
		opaqueColor,
		nearestClampSampler
	);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_2,
		colorHistoryRead,
		taaSampler
	);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_3,
		velocity,
		nearestClampSampler
	);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_4,
		prevVelocity,
		taaSampler
	);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_5,
		depthResolved,
		nearestClampSampler,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
	);
	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_6,
		prevDepthResolved,
		nearestClampSampler,
		VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL
	);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_WRITE_1,
		colorHistoryWrite
	);

	DispatchComputePass(frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::TAA),
		scope,
		frameCtx.m_descriptorWriter);

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		colorHistoryWrite,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	);

	ResourceManager::FlipColorHistory();
}

void RenderPasses::FXAAPass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.m_commandBuffer,
		scope.passID
	);

	auto& fxaaColor = ResourceManager::GetAAColor_Target();

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		fxaaColor,
		VK_IMAGE_LAYOUT_GENERAL
	);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_1,
		ResourceManager::GetToneMap_Target(),
		ResourceManager::GetLinearLODClamp_Sampler()
	);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_WRITE_1,
		fxaaColor
	);

	DispatchComputePass(frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::FXAA),
		scope,
		frameCtx.m_descriptorWriter);
}

void RenderPasses::SkyboxPass(
	FrameContext& frameCtx,
	GraphicsScope scope,
	Profiler& profiler,
	const bool hasVisibles)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.m_commandBuffer,
		scope.passID
	);
	auto& pipeline = Pipelines::GetHandle(PipelineID::Skybox);
	vkCmdBindPipeline(
		frameCtx.m_commandBuffer,
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

	BindPushConstant(invVp, frameCtx.m_commandBuffer);

	vkCmdDraw(frameCtx.m_commandBuffer, 3, 1, 0, 0);

	// Literally one triangle
	if (profiler.debugToggles.enableProfilerView) {
		profiler.addDirect(1, 1);
	}
}

void RenderPasses::TransparentResolvePass(
	FrameContext& frameCtx,
	ComputeScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.m_commandBuffer,
		scope.passID
	);

	auto& transparentResolve = ResourceManager::GetTransparentResolved_Target();
	auto& transparentAccum = ResourceManager::GetTransparentAccumulation_Target();
	auto& transparentReveal = ResourceManager::GetTransparentRevealage_Target();
	const auto nearestClampSampler = ResourceManager::GetNearestClamp_Sampler();

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		transparentResolve,
		VK_IMAGE_LAYOUT_GENERAL);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_1,
		transparentAccum,
		nearestClampSampler
	);

	frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_READ_2,
		transparentReveal,
		nearestClampSampler
	);

	 frameCtx.m_descriptorWriter.WritePushImage(
		PUSH_BINDING_WRITE_1,
		transparentResolve
	);

	DispatchComputePass(
		frameCtx.m_commandBuffer,
		Pipelines::GetHandle(PipelineID::TransparentResolve),
		scope,
		frameCtx.m_descriptorWriter
	);

	 ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		transparentResolve,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void RenderPasses::OpaqueForwardPass(
	FrameContext& frameCtx,
	GraphicsScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.m_commandBuffer,
		scope.passID
	);
	PipelineHandle pipeline{};
	if (!profiler.pipeOverride.enabled) {
		pipeline = Pipelines::GetHandle(PipelineID::OPAQUE); // default mesh pipeline
	}
	// Wireframe
	else {
		pipeline = Pipelines::GetHandle(profiler.pipeOverride.selectedID);
	}

	vkCmdBindPipeline(
		frameCtx.m_commandBuffer,
		pipeline.bindPoint,
		pipeline.pipeline);

	frameCtx.m_descriptorWriter.UpdatePushLayout(
		frameCtx.m_commandBuffer,
		pipeline.bindPoint,
		Pipelines::m_globalLayout.layout);

	BindPushConstant(Renderer::GetForwardPush(), frameCtx.m_commandBuffer);

	vkCmdDrawIndexedIndirect(frameCtx.m_commandBuffer,
		frameCtx.m_indirectDraws_GPU.m_buffer,
		frameCtx.m_opaqueDrawRange.firstCommand * DRAW_CMD_SIZE,
		frameCtx.m_opaqueDrawRange.commandCount,
		DRAW_CMD_SIZE
	);

	if (profiler.debugToggles.enableProfilerView) {
		const uint64_t trisOpaque = sumTrianglesIndirectRange(
			frameCtx.m_indirectDraws,
			frameCtx.m_opaqueDrawRange.firstCommand,
			frameCtx.m_opaqueDrawRange.commandCount,
			pipeline.topology);

		profiler.addOpaqueIndirect(/*commands*/1,
			/*sub-draws*/frameCtx.m_opaqueDrawRange.visibleCount,
			/*triangles*/trisOpaque);
	}
}

void RenderPasses::TransparentForwardPass(
	FrameContext& frameCtx,
	GraphicsScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.m_commandBuffer,
		scope.passID
	);
	auto& pipeline = Pipelines::GetHandle(PipelineID::TRANSPARENT);
	vkCmdBindPipeline(
		frameCtx.m_commandBuffer,
		pipeline.bindPoint,
		pipeline.pipeline);

	BindPushConstant(Renderer::GetForwardPush(), frameCtx.m_commandBuffer);

	vkCmdDrawIndexedIndirect(frameCtx.m_commandBuffer,
		frameCtx.m_indirectDraws_GPU.m_buffer,
		frameCtx.m_transparentDrawRange.firstCommand * DRAW_CMD_SIZE,
		frameCtx.m_transparentDrawRange.commandCount,
		DRAW_CMD_SIZE
	);

	if (profiler.debugToggles.enableProfilerView) {
		const uint64_t trisTransparent = sumTrianglesIndirectRange(
			frameCtx.m_indirectDraws,
			frameCtx.m_transparentDrawRange.firstCommand,
			frameCtx.m_transparentDrawRange.commandCount,
			pipeline.topology);

		profiler.addTransparentIndirect(/*commands*/1,
			/*sub-draws*/frameCtx.m_transparentDrawRange.visibleCount,
			/*triangles*/trisTransparent);
	}
}

void RenderPasses::ObbLineDebugPass(
	FrameContext& frameCtx,
	GraphicsScope scope,
	Profiler& profiler)
{
	auto tracyPass = profiler.profilePass(
		frameCtx,
		frameCtx.m_commandBuffer,
		scope.passID
	);
	std::vector<glm::vec3> allVerts;
	std::vector<uint32_t> drawOffsets;

	auto& resources = Engine::GetState().getGPUResources();
	const auto& meshes = resources.GetResgisteredMeshes().meshData;

	auto emitOBBVerts = [&](const Instance& inst) {
		const auto& aabb = meshes[inst.meshID].localAABB;
		const auto& matrix = RenderScene::_globalTransforms[inst.transformID];
		auto verts = GetOBBVertices(aabb, matrix);
		uint32_t offset = static_cast<uint32_t>(allVerts.size());
		drawOffsets.push_back(offset);
		allVerts.insert(allVerts.end(), verts.begin(), verts.end());
	};
	for (const auto& inst : frameCtx.m_visibleInstances) emitOBBVerts(inst);

	const auto allocator = resources.GetAllocator();

	const size_t totalSize = allVerts.size() * sizeof(glm::vec3);

	AllocatedBuffer obbVBO = BufferUtils::CreateBuffer(
		totalSize,
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_CPU_TO_GPU,
		allocator);
	ASSERT(obbVBO.m_allocInfo.pMappedData != nullptr);
	memcpy(obbVBO.m_mappedPtr, allVerts.data(), totalSize);

	auto aabbBuf = obbVBO.m_buffer;
	auto aabbAlloc = obbVBO.m_allocation;
	frameCtx.m_cpuDeletionQueue.PushFunction([aabbBuf, aabbAlloc, allocator]() mutable {
		BufferUtils::DestroyBuffer(aabbBuf, aabbAlloc, allocator);
	});

	auto& pipeline = Pipelines::GetHandle(PipelineID::OBBLine);
	vkCmdBindPipeline(
		frameCtx.m_commandBuffer,
		pipeline.bindPoint,
		pipeline.pipeline);

	const VkDeviceSize vtxOffset = 0;
	vkCmdBindVertexBuffers(frameCtx.m_commandBuffer, 0, 1, &obbVBO.m_buffer, &vtxOffset);

	static struct alignas(16) OBBPush {
		VkDeviceAddress vertexBuffer;
		uint32_t pad0[2];
	} pc{};
	pc.vertexBuffer = obbVBO.m_address;

	BindPushConstant(pc, frameCtx.m_commandBuffer);

	for (uint32_t i = 0; i < drawOffsets.size(); ++i) {
		uint32_t vertexOffset = drawOffsets[i];
		vkCmdDraw(frameCtx.m_commandBuffer, VERTS_LINE_COUNT, 1, vertexOffset, 0);
		if (profiler.debugToggles.enableProfilerView) {
			profiler.addDirect(1);
		}
	}
}

