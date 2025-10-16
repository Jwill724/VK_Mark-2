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
	GPUResources& gpuResouces,
	Profiler& profiler)
{
	SyncUtils::createTimelineSemaphore(_transferSync, device);

	if (GPU_ACCELERATION_ENABLED) {
		SyncUtils::createTimelineSemaphore(_computeSync, device);
	}

	_frameContexts = initFrameContexts(
		device,
		frameLayout,
		gpuResouces.getAllocator(),
		_framesInFlight,
		profiler.assetsLoaded
	);

	auto& debug = profiler.debugToggles;
	const auto& modelDataCounts = gpuResouces.modelDataCounts;

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

	VK_CHECK(vkResetCommandBuffer(frameCtx.commandBuffer, 0));

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
	cmdInfo.commandBuffer = frameCtx.commandBuffer;

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

void Renderer::recordRenderCommand(FrameContext& frameCtx, Profiler& profiler) {
	auto device = Backend::getDevice();
	auto& swp = Backend::getSwapchainDef();
	auto& draw = ResourceManager::getDrawImage();
	auto& msaa = ResourceManager::getMSAAImage();
	auto& depth = ResourceManager::getDepthImage();
	auto normalSampler = ResourceManager::getNormalSampler();

	const auto& debug = profiler.debugToggles;

	_drawExtent.width = std::min(swp.extent.width, draw.imageExtent.width);
	_drawExtent.height = std::min(swp.extent.height, draw.imageExtent.height);

	const VkExtent2D extent = { _drawExtent.width, _drawExtent.height };
	const VkExtent3D workgroupSize = { 16u, 16u, 1u };

	const auto unifiedSet = DescriptorSetOverwatch::getUnifiedDescriptor().descriptorSet;
	const VkDescriptorSet sets[2] {
		unifiedSet,
		frameCtx.set
	};

	auto& gpuResources = Engine::getState().getGPUResources();

	const auto& globalAddrsTableBuf = gpuResources.getAddressTableBuffer();

	bool hasVisibles = frameCtx.visibleCount > 0;

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

	VK_CHECK(vkBeginCommandBuffer(frameCtx.commandBuffer, &cmdBeginInfo));

	// Note: Currently only do cpu culling, once its in a compute this would need to be done way before main recording
	if (frameCtx.transformsBufferUploadNeeded && hasVisibles) {
		BarrierUtils::acquireShaderReadQ(frameCtx.commandBuffer, globalAddrsTableBuf);
		frameCtx.transformsBufferUploadNeeded = false; // Should only set back to false in here
	}

	if (hasVisibles) {
		BarrierUtils::acquireShaderReadQ(frameCtx.commandBuffer, frameCtx.addressTableBuffer);
	}

	vkCmdBindDescriptorSets(frameCtx.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		Pipelines::_globalLayout.layout, 0, 2, sets, 0, nullptr);

	vkCmdBindDescriptorSets(frameCtx.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		Pipelines::_globalLayout.layout, 0, 2, sets, 0, nullptr);

	// color transition
	ImageUtils::transitionImage(
		frameCtx.commandBuffer,
		draw.image,
		draw.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	// Used in opaque shading to determine if on
	if (hasVisibles) {
		// Always bind global index buffer at start of visibles frame
		const auto indexBuffer = Engine::getState().getGPUResources().getGPUAddrsBuffer(AddressBufferType::Index).buffer;
		vkCmdBindIndexBuffer(frameCtx.commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

		// =====================
		// === DEPTH PREPASS ===
		RenderPasses::depthPrePass(frameCtx, Pipelines::getHandle(PipelineID::DepthPrepass));

		// ================
		// === CSM PASS ===
		if (debug.enableShadows) {
			RenderPasses::shadowCSMPass(frameCtx, Pipelines::getHandle(PipelineID::ShadowCSM));
		}

		// =================
		// === SSAO PASS ===
		if (debug.enableSSAO) {
			const auto& proj = RenderScene::getCurrentSceneData().proj;
			const auto& winExtent = Engine::getWindowExtent();

			static struct alignas(16) SSAOPush {
				glm::mat4 invProj;
				glm::vec2 screenSize;
				glm::vec2 invScreenSize;
				float aoRadius;
				float bias;
				float intensity;
				int blurRadius;
				uint32_t sampleCount;
				float pad0[3]{};
			} ssaoPc{};

			ssaoPc.invProj = glm::inverse(proj);
			ssaoPc.screenSize = glm::vec2(
				static_cast<float>(winExtent.width),
				static_cast<float>(winExtent.height)
			);
			ssaoPc.invScreenSize = 1.0f / ssaoPc.screenSize;

			ssaoPc.sampleCount = profiler.ssaoSettings.sampleCount;
			ssaoPc.aoRadius = profiler.ssaoSettings.aoRadius;
			ssaoPc.bias = profiler.ssaoSettings.bias;
			ssaoPc.intensity = profiler.ssaoSettings.intensity;
			ssaoPc.blurRadius = profiler.ssaoSettings.blurRadius;

			RenderPasses::ComputeDispatchScope ssaoScope;
			ssaoScope.setPush(&ssaoPc);
			ssaoScope.extent = extent;
			ssaoScope.workgroupSize = workgroupSize;
			ssaoScope.calculateGroups();

			RenderPasses::SSAOPass(frameCtx, ssaoScope);
		}
	}


	// === MAIN FORWARD SHADING PASS ===
	AttachmentDesc colorAttach{};
	if (MSAA_ENABLED) {
		colorAttach.imageView = msaa.imageView;
		colorAttach.layout = msaa.initialLayout;
		colorAttach.resolveView = draw.imageView;
		colorAttach.resolveLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttach.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
		colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	}
	else {
		colorAttach.imageView = draw.imageView;
		colorAttach.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		colorAttach.resolveMode = VK_RESOLVE_MODE_NONE;
		colorAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	}

	AttachmentDesc depthAttach{};
	depthAttach.imageView = depth.imageView;
	depthAttach.layout = depth.initialLayout;
	depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthAttach.clearValue.depthStencil.depth = 1.0f;

	RenderPasses::GraphicsRenderScope geometryScope;

	RenderPasses::beginRendering(
		frameCtx.commandBuffer,
		{ colorAttach, depthAttach },
		{ extent },
		geometryScope);

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

		if (debug.enableSSAO) {
			auto& ssaoBlurV = ResourceManager::getSSAOBlurVImage(); // Final ao image

			frameCtx.descriptorWriter.writePushImage(
				PUSH_BINDING_INPUT_TEX,
				ssaoBlurV.imageView,
				ResourceManager::getSSAOSampler()
			);
		}
		else {
			// Pipeline needs a push layout so dummy white view and normal sampler
			auto& white = ResourceManager::getWhiteMat();
			frameCtx.descriptorWriter.writePushImage(
				PUSH_BINDING_INPUT_TEX,
				white.imageView,
				normalSampler
			);
		}

		RenderPasses::opaqueMeshPass(frameCtx, pipeline, profiler);


		// ========================
		// === TRANSPARENT PASS ===
		if (frameCtx.transparentRange.visibleCount > 0) {
			if (!profiler.pipeOverride.enabled) {
				pipeline = Pipelines::getHandle(PipelineID::Transparent);
			}

			RenderPasses::transparentMeshPass(frameCtx, pipeline, profiler);
		}

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

	RenderPasses::endRendering(frameCtx.commandBuffer);


	// === TONE MAP PASS ===
	auto& toneMap = ResourceManager::getToneMappingImage();
	if (debug.enableTonemap) {
		RenderPasses::ComputeDispatchScope tonemapPass;
		tonemapPass.setPush(&ResourceManager::toneMappingData);
		tonemapPass.extent = extent;
		tonemapPass.workgroupSize = workgroupSize;
		tonemapPass.calculateGroups();

		RenderPasses::ToneMapPass(frameCtx, tonemapPass, toneMap);

		ImageUtils::transitionImage(
			frameCtx.commandBuffer,
			swp.images[frameCtx.swapchainImageIndex],
			toneMap.imageFormat,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	}
	else {
		ImageUtils::transitionImage(
			frameCtx.commandBuffer,
			draw.image,
			draw.imageFormat,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

		ImageUtils::transitionImage(
			frameCtx.commandBuffer,
			swp.images[frameCtx.swapchainImageIndex],
			draw.imageFormat,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	}

	ImageUtils::copyImageToImage(
		frameCtx.commandBuffer,
		debug.enableTonemap ? toneMap.image : draw.image,
		swp.images[frameCtx.swapchainImageIndex],
		{ _drawExtent.width, _drawExtent.height },
		swp.extent
	);

	if (debug.enableSettings || debug.enableStats) {
		// Transition swapchain to COLOR_ATTACHMENT_OPTIMAL for ImGui

		ImageUtils::transitionImage(
			frameCtx.commandBuffer,
			swp.images[frameCtx.swapchainImageIndex],
			swp.imageFormat,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		EditorImgui::drawImgui(
			frameCtx.commandBuffer,
			swp.imageViews[frameCtx.swapchainImageIndex],
			swp.extent,
			false);

		ImageUtils::transitionImage(
			frameCtx.commandBuffer,
			swp.images[frameCtx.swapchainImageIndex],
			swp.imageFormat,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	}
	else {
		ImageUtils::transitionImage(
			frameCtx.commandBuffer,
			swp.images[frameCtx.swapchainImageIndex],
			draw.imageFormat,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	}

	VK_CHECK(vkEndCommandBuffer(frameCtx.commandBuffer));
}


void Renderer::cleanupRenderer(const VkDevice device, const VmaAllocator alloc) {
	cleanupFrameContexts(_frameContexts, device, alloc);

	if (_transferSync.semaphore != VK_NULL_HANDLE)
		vkDestroySemaphore(device, _transferSync.semaphore, nullptr);

	if (_computeSync.semaphore != VK_NULL_HANDLE)
		vkDestroySemaphore(device, _computeSync.semaphore, nullptr);
}