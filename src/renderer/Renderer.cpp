#include "pch.h"

#include "Renderer.h"
#include "engine/platform/profiler/EditorImgui.h"
#include "scene/RenderScene.h"
#include "utils/BufferUtils.h"
#include "core/AssetManager.h"
#include "utils/SyncUtils.h"
#include "renderer/passes/RenderPasses.h"

namespace Renderer {
	VkExtent3D _drawExtent;
	std::mutex _drawExtentMutex;

	const VkExtent3D getDrawExtent() {
		std::scoped_lock lock(_drawExtentMutex);
		return _drawExtent;
	}
	void setDrawExtent(VkExtent3D extent) {
		std::scoped_lock lock(_drawExtentMutex);
		_drawExtent = extent;
	}

	uint32_t _frameNumber{ 0 };
	uint32_t _framesInFlight{ 0 };

	std::mutex _frameAccessMutex;

	FrameContext& getCurrentFrame() {
		std::scoped_lock lock(_frameAccessMutex);
		return *_frameContexts[_frameNumber % _framesInFlight];
	}

	TimelineSync _transferSync;
	TimelineSync _computeSync;
}

void Renderer::initRenderer(
	const VkDevice device,
	const VkDescriptorSetLayout frameLayout,
	GPUResources& gpuResources,
	Profiler& profiler)
{
	SyncUtils::createTimelineSemaphore(_transferSync, device);

	if (GPU_ACCELERATION_ENABLED) {
		SyncUtils::createTimelineSemaphore(_computeSync, device);
	}

	_frameContexts = initFrameContexts(
		device,
		frameLayout,
		gpuResources.getAllocator(),
		_framesInFlight,
		gpuResources.assetsLoaded
	);

	auto& debug = profiler.debugToggles;
	const auto& modelDataCounts = gpuResources.modelDataCounts;

	debug.indexCount = modelDataCounts.totalIndexCount;
	debug.vertexCount = modelDataCounts.totalVertexCount;
	debug.materialCount = modelDataCounts.totalMaterialCount;
	debug.transformCount = modelDataCounts.totalTransformCount;
	debug.meshCount = modelDataCounts.totalMeshCount;
}

// =============================================
// === CLEAR DATA AND AQUIRE SWAPCHAIN INDEX ===

void Renderer::prepareFrameContext(FrameContext& frameCtx) {
	auto device = Backend::getDevice();
	auto& swapDef = Backend::getSwapchainDef();

	VkFence fence = swapDef.inFlightFences[frameCtx.frameIndex];
	VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
	VK_CHECK(vkResetFences(device, 1, &fence));

	uint32_t imageIndex = 0;
	frameCtx.swapchainResult = vkAcquireNextImageKHR(
		device,
		swapDef.swapchain,
		UINT64_MAX,
		swapDef.imageAvailableSemaphores[frameCtx.frameIndex],
		VK_NULL_HANDLE,
		&imageIndex);

	if (frameCtx.swapchainResult == VK_ERROR_OUT_OF_DATE_KHR ||
		frameCtx.swapchainResult == VK_SUBOPTIMAL_KHR)
	{
		Backend::getGraphicsQueue().waitIdle();
		Backend::resizeSwapchain();
		return;
	}
	ASSERT(frameCtx.swapchainResult == VK_SUCCESS && "Failed to acquire swapchain image!");

	frameCtx.swapchainImageIndex = imageIndex;

	// Mark image as in use
	swapDef.imageInFlightFrame[imageIndex] = frameCtx.frameIndex;

	VK_CHECK(vkResetCommandBuffer(frameCtx.cmdBuffer, 0));

	frameCtx.freeStashedCmds(device);
	frameCtx.stagingHead = 0;

	frameCtx.cpuDeletion.flush();
}

// ===============================================
// === SYNC FRAME SEMAPHORES AND PRESENT FRAME ===

void Renderer::submitFrame(FrameContext& frameCtx, GPUResources& resources) {
	auto& swapDef = Backend::getSwapchainDef();
	uint32_t imageIndex = frameCtx.swapchainImageIndex;

	VkSemaphore presentSem = swapDef.imageAvailableSemaphores[frameCtx.frameIndex];

	// Use image-indexed render finished semaphore and fence
	VkSemaphore renderSem = swapDef.renderFinishedSemaphores[imageIndex];

	VkFence fence = swapDef.inFlightFences[frameCtx.frameIndex];

	std::vector<VkSemaphoreSubmitInfo> waitInfos;

	// Wait on image acquired semaphore
	VkSemaphoreSubmitInfo waitImageAvailable{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
	waitImageAvailable.semaphore = presentSem;
	waitImageAvailable.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	waitInfos.push_back(waitImageAvailable);

	// Wait on transfer timeline only up to the first buffer consumers
	if (frameCtx.transferWaitValue != UINT64_MAX &&
		frameCtx.transferWaitValue <= _transferSync.signalValue)
	{
		ASSERT(frameCtx.transferWaitValue <= _transferSync.signalValue &&
			"Invalid transfer wait: waiting on unsignaled or future timeline value!");

		VkSemaphoreSubmitInfo waitTransfer{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
		waitTransfer.semaphore = _transferSync.semaphore;
		waitTransfer.value = frameCtx.transferWaitValue;
		waitTransfer.stageMask =
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT |
			VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT |
			VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT; // earliest consumers of uploaded data
		waitInfos.push_back(waitTransfer);
	}

	if (GPU_ACCELERATION_ENABLED) {
		if (frameCtx.computeWaitValue != UINT64_MAX &&
			frameCtx.computeWaitValue <= _computeSync.signalValue)
		{
			ASSERT(frameCtx.computeWaitValue <= _computeSync.signalValue &&
				"Invalid compute wait: waiting on unsignaled or future timeline value!");

			VkSemaphoreSubmitInfo waitCompute{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
			waitCompute.semaphore = _computeSync.semaphore;
			waitCompute.value = frameCtx.computeWaitValue;
			waitCompute.stageMask =
				VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT |
				VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
			waitInfos.push_back(waitCompute);
		}
	}

	VkCommandBufferSubmitInfo cmdInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
	cmdInfo.commandBuffer = frameCtx.cmdBuffer;

	// Signal render finished semaphore
	VkSemaphoreSubmitInfo signalRender{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
	signalRender.semaphore = renderSem;
	signalRender.stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;

	VkSubmitInfo2 submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO_2 };
	submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitInfos.size());
	submitInfo.pWaitSemaphoreInfos = waitInfos.data();
	submitInfo.commandBufferInfoCount = 1;
	submitInfo.pCommandBufferInfos = &cmdInfo;
	submitInfo.signalSemaphoreInfoCount = 1;
	submitInfo.pSignalSemaphoreInfos = &signalRender;

	auto& gQueue = Backend::getGraphicsQueue();
	auto& pQueue = Backend::getPresentQueue();

	VK_CHECK(vkQueueSubmit2(gQueue.queue, 1, &submitInfo, fence));

	// Present using the image-indexed renderSem
	VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &renderSem;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &swapDef.swapchain;
	presentInfo.pImageIndices = &imageIndex;

	frameCtx.swapchainResult = vkQueuePresentKHR(pQueue.queue, &presentInfo);
	if (frameCtx.swapchainResult == VK_ERROR_OUT_OF_DATE_KHR || frameCtx.swapchainResult == VK_SUBOPTIMAL_KHR) {
		if (gQueue.queue != pQueue.queue)
			pQueue.waitIdle();
		else
			gQueue.waitIdle();

		Backend::resizeSwapchain();
		auto& queue = resources.getRenderTargetDQueue();
		queue.flush(); // Clear render targets
		// Recreate all render targets with extent updates
		ResourceManager::initRenderTargets(
			Backend::getDevice(),
			queue,
			resources.getAllocator(),
			_drawExtent); // New extent
	}
	else {
		ASSERT(frameCtx.swapchainResult == VK_SUCCESS && "Failed to present swapchain image!");
	}

	_frameNumber++;
}

// ==============================================
// === MAIN GRAPHICS RECORDING FOR ALL PASSES ===
// ==============================================
// All recorded in a single graphics queue

void Renderer::recordRenderCommand(FrameContext& frameCtx, Profiler& profiler) {
	const auto device = Backend::getDevice();
	const auto& swp = Backend::getSwapchainDef();
	const auto& opaque = ResourceManager::getOpaqueImage();
	const auto& transparent = ResourceManager::getTransparentImage();
	const auto& toneMap = ResourceManager::getToneMapImage();
	const auto& msaa = ResourceManager::getMSAAImage();
	const auto& depth = ResourceManager::getDepthImage();
	const auto& aoFinal = ResourceManager::getAORawImage();
	const auto aoSampler = ResourceManager::getAOSampler();
	const auto& depthResolved = ResourceManager::getDepthResolvedImage();
	const auto& volLight = ResourceManager::getVolumetricLightImage();

	auto& debug = profiler.debugToggles;

	// Small hack to fix dumb gpu validation layer issues.
	// Fix some time later...
	if (_frameNumber < 2) {
		if (_frameNumber == 0) {
			debug.aoMode = AO_OFF;
			debug.enableShadows = 0;
		}
		else {
			debug.aoMode = AO_GTAO;
			debug.enableShadows = 1;
		}
	}

	const VkExtent2D fullExtent = { _drawExtent.width, _drawExtent.height };
	const VkExtent3D workgroupSize = { 16u, 16u, 1u };

	const auto& winExtent = Engine::getWindowExtent();

	const auto unifiedSet = DescriptorSetOverwatch::getUnifiedDescriptor().descriptorSet;
	const VkDescriptorSet sets[2]{
		unifiedSet,
		frameCtx.set
	};

	auto& gpuResources = Engine::getState().getGPUResources();

	const auto& globalAddrsTableBuf = gpuResources.getAddressTableBuffer();

	auto& scene = RenderScene::getCurrentSceneData();

	bool hasVisibles = frameCtx.visibleCount > 0;

	bool isTemporalValid = (_frameNumber > 0 && scene.temporal.y == 1);

	if (frameCtx.transformsBufferUploadNeeded && hasVisibles) {
		// Update the global set for transforms
		frameCtx.descriptorWriter.writeBuffer(
			ADDRESS_TABLE_BINDING,
			globalAddrsTableBuf.buffer,
			sizeof(GPUAddressTable),
			0,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			unifiedSet);

		frameCtx.descriptorWriter.updateSet(device, unifiedSet);
	}

	frameCtx.writeFrameDescriptors(device);

	VkCommandBufferBeginInfo cmdBeginInfo{};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(frameCtx.cmdBuffer, &cmdBeginInfo));

	// Note: Currently only do cpu culling, once its in a compute this would need to be done way before main recording
	if (frameCtx.transformsBufferUploadNeeded && hasVisibles) {
		const auto& transformsBuf = gpuResources.getGPUAddrsBuffer(AddressBufferType::Transforms);
		const auto& prevTransformsBuf = gpuResources.getGPUAddrsBuffer(AddressBufferType::PrevTransforms);
		BarrierUtils::acquireShaderReadQ(frameCtx.cmdBuffer, transformsBuf);
		BarrierUtils::acquireShaderReadQ(frameCtx.cmdBuffer, prevTransformsBuf);
		frameCtx.transformsBufferUploadNeeded = false; // Should only set back to false in here
	}

	if (hasVisibles) {
		BarrierUtils::acquireShaderReadQ(frameCtx.cmdBuffer, frameCtx.visibleInstancesBuffer);
		BarrierUtils::acquireIndirectQ(frameCtx.cmdBuffer, frameCtx.indirectDrawsBuffer);
	}

	vkCmdBindDescriptorSets(frameCtx.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		Pipelines::_globalLayout.layout, 0, 2, sets, 0, nullptr);

	vkCmdBindDescriptorSets(frameCtx.cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		Pipelines::_globalLayout.layout, 0, 2, sets, 0, nullptr);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		opaque.image,
		opaque.format,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	// Used in opaque shading to determine if on
	if (hasVisibles) {
		// Always bind global index buffer at start of visibles frame
		const auto indexBuffer = gpuResources.getGPUAddrsBuffer(AddressBufferType::Index).buffer;
		vkCmdBindIndexBuffer(frameCtx.cmdBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

		// =====================
		// === DEPTH PREPASS ===
		RenderPasses::depthPrePass(
			frameCtx,
			Pipelines::getHandle(PipelineID::DepthPrepass),
			isTemporalValid);

		// ==========================
		// === DEPTH PYRAMID PASS ===
		RenderPasses::depthPyramidPass(frameCtx);

		// =================
		// === GTAO PASS ===
		if (debug.aoMode == AO_GTAO) {
			auto& gtaoPush = profiler.gtaoSettings;

			const auto& proj = scene.proj;

			gtaoPush.tanHalfFov.x = 1.0f / proj[0][0];
			gtaoPush.tanHalfFov.y = 1.0f / proj[1][1];

			gtaoPush.ndcToViewMul = { gtaoPush.tanHalfFov.x * 2.0f, gtaoPush.tanHalfFov.y * -2.0f };
			gtaoPush.ndcToViewAdd = { gtaoPush.tanHalfFov.x * -1.0f, gtaoPush.tanHalfFov.y * 1.0f };

			gtaoPush.pixelSize = {
				1.0f / static_cast<float>(winExtent.width),
				1.0f / static_cast<float>(winExtent.height)
			};

			gtaoPush.ndcToViewMul_x_PixelSize = gtaoPush.ndcToViewMul * gtaoPush.pixelSize;

			RenderPasses::ComputeDispatchScope gtaoScope;
			gtaoScope.setPush(gtaoPush);
			gtaoScope.extent = fullExtent;
			gtaoScope.workgroupSize = workgroupSize;

			RenderPasses::GTAOPass(frameCtx, gtaoScope, isTemporalValid);
		}
		// =================
		// === SSAO PASS ===
		else if (debug.aoMode == AO_SSAO) {
			auto& ssaoPush = profiler.ssaoSettings;
			ssaoPush.screenSize = glm::vec2(
				static_cast<float>(winExtent.width),
				static_cast<float>(winExtent.height)
			);
			ssaoPush.invScreenSize = 1.0f / ssaoPush.screenSize;

			RenderPasses::ComputeDispatchScope ssaoScope;
			ssaoScope.setPush(ssaoPush);
			ssaoScope.extent = fullExtent;
			ssaoScope.workgroupSize = workgroupSize;

			RenderPasses::SSAOPass(frameCtx, ssaoScope);
		}
		// AO OFF
		else {
			ImageUtils::transitionImage(
				frameCtx.cmdBuffer,
				aoFinal.image,
				aoFinal.format,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		}

		// ================
		// === CSM PASS ===
		if (debug.enableShadows) {
			RenderPasses::shadowCSMPass(frameCtx, Pipelines::getHandle(PipelineID::ShadowCSM));
		}
	}

	// =================================
	// === MAIN FORWARD SHADING PASS ===
	AttachmentDesc opaqueAttach{};
	if (MSAA_ENABLED) {
		opaqueAttach.imageView = msaa.imageView;
		opaqueAttach.layout = msaa.initialLayout;
		opaqueAttach.resolveView = opaque.imageView;
		opaqueAttach.resolveLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		opaqueAttach.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
		opaqueAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		opaqueAttach.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	}
	else {
		opaqueAttach.imageView = opaque.imageView;
		opaqueAttach.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		opaqueAttach.resolveMode = VK_RESOLVE_MODE_NONE;
		opaqueAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		opaqueAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	}

	AttachmentDesc depthAttach{};
	depthAttach.imageView = depth.imageView;
	depthAttach.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthAttach.clearValue.depthStencil.depth = 0.0f;

	RenderPasses::GraphicsRenderScope opaqueScope;

	RenderPasses::beginRendering(
		frameCtx.cmdBuffer,
		{ opaqueAttach, depthAttach },
		{ fullExtent },
		opaqueScope);

	// ===================
	// === SKYBOX PASS ===
	// Sky box draw always occurs
	RenderPasses::skyboxPass(frameCtx, Pipelines::getHandle(PipelineID::Skybox), profiler);

	if (hasVisibles) {
		// Can always assume opaque draw
		PipelineHandle pipeline{};

		// ===================
		// === OPAQUE PASS ===
		if (!profiler.pipeOverride.enabled) {
			pipeline = Pipelines::getHandle(PipelineID::Opaque); // default mesh pipeline
		}
		else {
			pipeline = Pipelines::getHandle(profiler.pipeOverride.selectedID);
		}

		if (debug.aoMode == AO_GTAO && isTemporalValid) {
			// Temporally stabilized
			const auto& aoTemporal = ResourceManager::getAOHistoryWrite();
			frameCtx.descriptorWriter.writePushImage(
				PUSH_BINDING_INPUT_1_TEX,
				aoTemporal.imageView,
				aoSampler
			);
		}
		else {
			frameCtx.descriptorWriter.writePushImage(
				PUSH_BINDING_INPUT_1_TEX,
				aoFinal.imageView,
				aoSampler
			);
		}

		RenderPasses::opaqueMeshPass(frameCtx, pipeline, profiler);

		// ======================
		// === OBB DEBUG PASS ===
		if (debug.enableOBBs) {
			RenderPasses::obbLinePass(frameCtx, Pipelines::getHandle(PipelineID::OBBLine), profiler);
		}

		// ============================
		// === CASCADEVP DEBUG PASS ===
		if (debug.enableShadows && debug.enableCascadeVPs) {
			RenderPasses::CascadeVPLinePass(frameCtx, Pipelines::getHandle(PipelineID::CascadeVPLine), profiler);
		}
	}

	RenderPasses::endRendering(frameCtx.cmdBuffer);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		opaque.image,
		opaque.format,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);


	// ================================
	// === VOLUMETRIC LIGHTING PASS ===
	if (hasVisibles && debug.enableVolumetrics && debug.enableShadows) {
		glm::uvec2 halfRes{ volLight.extent.width, volLight.extent.height };

		auto& volLightPush = profiler.volLightSettings;
		volLightPush.pixelSize = {
			1.0f / static_cast<float>(halfRes.x),
			1.0f / static_cast<float>(halfRes.y)
		};

		RenderPasses::ComputeDispatchScope volLightScope;
		volLightScope.setPush(volLightPush);
		volLightScope.extent = { halfRes.x, halfRes.y };
		volLightScope.workgroupSize = workgroupSize;

		RenderPasses::volumetricLightingPass(frameCtx, volLightScope);
	}

	// ========================
	// === TRANSPARENT PASS ===
	bool transparentVisible = false;
	if (frameCtx.transparentRange.visibleCount > 0 && hasVisibles) {
		transparentVisible = true;

		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			transparent.image,
			transparent.format,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		AttachmentDesc transparentAttach{};
		transparentAttach.imageView = transparent.imageView;
		transparentAttach.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		transparentAttach.resolveMode = VK_RESOLVE_MODE_NONE;
		transparentAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		transparentAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

		depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

		if (MSAA_ENABLED) {
			ImageUtils::transitionImage(
				frameCtx.cmdBuffer,
				depthResolved.image,
				depthResolved.format,
				VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
				VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

			depthAttach.imageView = depthResolved.imageView;
		}

		RenderPasses::GraphicsRenderScope transparentScope;

		RenderPasses::beginRendering(
			frameCtx.cmdBuffer,
			{ transparentAttach, depthAttach },
			{ fullExtent },
			transparentScope);

		RenderPasses::transparentMeshPass(frameCtx, Pipelines::getHandle(PipelineID::Transparent), profiler);

		RenderPasses::endRendering(frameCtx.cmdBuffer);

		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			transparent.image,
			transparent.format,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
	}

	// =====================
	// === EXPOSURE PASS ===
	RenderPasses::ComputeDispatchScope defaultScope;
	defaultScope.extent = fullExtent;
	defaultScope.workgroupSize = workgroupSize;
	const auto& luminanceBuf = gpuResources.getGPUAddrsBuffer(AddressBufferType::Luminance);
	RenderPasses::exposurePass(frameCtx, defaultScope, luminanceBuf, transparentVisible);

	// =======================
	// === LENS FLARE PASS ===
	if (debug.enableLensFlare && hasVisibles) {
		auto& lensFlarePush = profiler.lensFlareSettings;
		const auto& brightFlare = ResourceManager::getFlareBrightImage();
		VkExtent2D quarterRes = {
			brightFlare.extent.width,
			brightFlare.extent.height
		};

		lensFlarePush.fullRes = glm::vec2(
			static_cast<float>(winExtent.width),
			static_cast<float>(winExtent.height)
		);
		lensFlarePush.invFullRes = 1.0f / lensFlarePush.fullRes;

		lensFlarePush.outputRes = glm::vec2(
			static_cast<float>(quarterRes.width),
			static_cast<float>(quarterRes.height)
		);
		lensFlarePush.invOutputRes = 1.0f / lensFlarePush.outputRes;

		glm::vec3 cameraWorldPos = glm::vec3(scene.cameraPos);
		glm::vec3 sunDirWorld = glm::normalize(glm::vec3(scene.sunlightDirection));
		glm::vec3 sunWorldPos = cameraWorldPos + sunDirWorld * 10000.0f;
		glm::vec4 clip = scene.proj * scene.view * glm::vec4(sunWorldPos, 1.0f);
		bool inFront = (clip.w > 0.0f);

		glm::vec3 ndc = glm::vec3(clip) / clip.w;
		glm::vec2 uv{};
		uv.x = ndc.x * 0.5f + 0.5f;
		uv.y = 0.5f - ndc.y * 0.5f;

		bool onScreen =
			(uv.x >= 0.0f && uv.x <= 1.0f) &&
			(uv.y >= 0.0f && uv.y <= 1.0f);

		lensFlarePush.sunUv = uv;
		lensFlarePush.sunVisible = (inFront && onScreen) ? 1.0f : 0.0f;

		RenderPasses::ComputeDispatchScope lensFlareScope;
		lensFlareScope.setPush(lensFlarePush);
		lensFlareScope.extent = quarterRes;
		lensFlareScope.workgroupSize = workgroupSize;

		RenderPasses::lensFlarePass(frameCtx, lensFlareScope,
			transparentVisible,
			hasVisibles,
			debug);
	}

	// =====================
	// === TONE MAP PASS ===
	RenderPasses::toneMapPass(frameCtx, defaultScope,
		transparentVisible,
		hasVisibles,
		debug);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		swp.images[frameCtx.swapchainImageIndex],
		swp.format,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	ImageUtils::copyImageToImage(
		frameCtx.cmdBuffer,
		toneMap.image,
		swp.images[frameCtx.swapchainImageIndex],
		fullExtent,
		swp.extent,
		swp.format
	);

	if (debug.enableSettings || debug.enableStats) {
		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			swp.images[frameCtx.swapchainImageIndex],
			swp.format,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		EditorImgui::drawImgui(
			frameCtx.cmdBuffer,
			swp.imageViews[frameCtx.swapchainImageIndex],
			swp.extent,
			false);

		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			swp.images[frameCtx.swapchainImageIndex],
			swp.format,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	}
	else {
		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			swp.images[frameCtx.swapchainImageIndex],
			swp.format,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	}

	VK_CHECK(vkEndCommandBuffer(frameCtx.cmdBuffer));
}


void Renderer::cleanupRenderer(const VkDevice device, const VmaAllocator alloc) {
	cleanupFrameContexts(_frameContexts, device, alloc);

	if (_transferSync.semaphore != VK_NULL_HANDLE)
		vkDestroySemaphore(device, _transferSync.semaphore, nullptr);

	if (_computeSync.semaphore != VK_NULL_HANDLE)
		vkDestroySemaphore(device, _computeSync.semaphore, nullptr);
}