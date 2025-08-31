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
	bool isAssetsLoaded)
{
	SyncUtils::createTimelineSemaphore(_transferSync, device);

	if (GPU_ACCELERATION_ENABLED) {
		SyncUtils::createTimelineSemaphore(_computeSync, device);
	}

	_frameContexts = initFrameContexts(
		device,
		frameLayout,
		gpuResouces.getAllocator(),
		gpuResouces.stats,
		_framesInFlight,
		isAssetsLoaded
	);
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

void Renderer::submitFrame(FrameContext& frameCtx) {
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
	auto& toneMap = ResourceManager::getToneMappingImage();
	auto& depthResolved = ResourceManager::getDepthResolvedImage();
	auto& normal = ResourceManager::getNormalImage();
	auto& ssaoImg = ResourceManager::getSSAOImage();
	auto& noiseTex = ResourceManager::getSSAONoiseImage();
	auto& ssaoBlurH = ResourceManager::getSSAOBlurHImage();
	auto& ssaoBlurV = ResourceManager::getSSAOBlurVImage();

	auto samplerDepth = ResourceManager::getSamplerDepth();
	auto samplerNormal = ResourceManager::getSamplerNormal();
	auto samplerNoise = ResourceManager::getSamplerNoise();
	auto samplerSSAO = ResourceManager::getSamplerSSAO();

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

	bool msaaEnabled = MSAA_ENABLED;
	bool hasVisibles = frameCtx.visibleCount > 0;

	if (frameCtx.transformsBufferUploadNeeded && hasVisibles) {
		// Update the global set for transforms
		frameCtx.descriptorWriter.clear();
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

	// color, depth and msaa transitions
	ImageUtils::transitionImage(
		frameCtx.commandBuffer,
		draw.image,
		draw.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	if (msaaEnabled) {
		ImageUtils::transitionImage(
			frameCtx.commandBuffer,
			msaa.image,
			msaa.imageFormat,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	}

	ImageUtils::transitionImage(
		frameCtx.commandBuffer,
		depth.image,
		depth.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

	// Used in opaque shading to determine if on
	frameCtx.drawDataPC.ssaoEnabled = profiler.debugToggles.enableSSAO;
	if (hasVisibles) {
		// Always bind global index buffer at start of visibles frame
		const auto indexBuffer = Engine::getState().getGPUResources().getGPUAddrsBuffer(AddressBufferType::Index).buffer;
		vkCmdBindIndexBuffer(frameCtx.commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);


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


		// === DEPTH PREPASS ===
		{
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
				extent,
				depthScope);

			RenderPasses::depthPrePass(frameCtx, Pipelines::getHandle(PipelineID::DepthPrepass));
			RenderPasses::endRendering(frameCtx.commandBuffer);
		}

		// =================
		// === SSAO PASS ===

		if (profiler.debugToggles.enableSSAO) {
			frameCtx.descriptorWriter.enablePushDescriptor = true;

			ImageUtils::transitionImage(
				frameCtx.commandBuffer,
				depthResolved.image,
				depthResolved.imageFormat,
				VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			// Transition normals to sampled
			ImageUtils::transitionImage(
				frameCtx.commandBuffer,
				normal.image,
				normal.imageFormat,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

			// Transition SSAO output to storage writable
			ImageUtils::transitionImage(
				frameCtx.commandBuffer,
				ssaoImg.image,
				ssaoImg.imageFormat,
				VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_GENERAL);

			// Push writing for main ssao pass
			frameCtx.descriptorWriter.clear();

			VkDescriptorImageInfo depthInfo{};
			depthInfo.imageView = depthResolved.imageView;
			depthInfo.sampler = samplerDepth;
			depthInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			frameCtx.descriptorWriter.writePushImage(
				PUSH_DEPTH_TEX_BINDING,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				depthInfo
			);

			// normal
			VkDescriptorImageInfo normalInfo{};
			normalInfo.imageView = normal.imageView;
			normalInfo.sampler = samplerNormal;
			normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			frameCtx.descriptorWriter.writePushImage(
				PUSH_NORMAL_TEX_BINDING,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				normalInfo
			);

			// noise texture
			VkDescriptorImageInfo noiseInfo{};
			noiseInfo.imageView = noiseTex.imageView;
			noiseInfo.sampler = samplerNoise;
			noiseInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			frameCtx.descriptorWriter.writePushImage(
				PUSH_NOISE_TEX_BINDING,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				noiseInfo
			);

			// kernel UBO
			frameCtx.descriptorWriter.writePushBuffer(
				PUSH_SSAO_KERNEL_BINDING,
				gpuResources.ssaoKernelBuffer.buffer,
				sizeof(glm::vec4[ResourceManager::_kernelBlockSize]),
				0,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
			);

			// SSAO output
			VkDescriptorImageInfo ssaoOutInfo{};
			ssaoOutInfo.imageView = ssaoImg.imageView;
			ssaoOutInfo.sampler = VK_NULL_HANDLE;
			ssaoOutInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

			frameCtx.descriptorWriter.writePushImage(
				PUSH_SSAO_OUTPUT_TEX_BINDING,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				ssaoOutInfo
			);

			const auto& proj = RenderScene::getCurrentSceneData().proj;

			struct alignas(16) SSAOPush {
				glm::mat4 invProj;
				glm::vec2 screenSize;
				glm::vec2 invScreenSize;
				float aoRadius;
				float bias;
				float intensity;
				int blurRadius;
				unsigned int sampleCount;
				float pad0[3];
			} ssaoPc{};
			ssaoPc.invProj = glm::inverse(proj);
			ssaoPc.screenSize = glm::vec2(
				static_cast<float>(extent.width),
				static_cast<float>(extent.height)
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
			frameCtx.descriptorWriter.clear();

			VkDescriptorImageInfo ssaoInputInfo{};
			ssaoInputInfo.imageView = ssaoImg.imageView;
			ssaoInputInfo.sampler = samplerSSAO;
			ssaoInputInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			frameCtx.descriptorWriter.writePushImage(
				PUSH_SSAO_INPUT_TEX_BINDING,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				ssaoInputInfo
			);

			VkDescriptorImageInfo ssaoBlurHInfo{};
			ssaoBlurHInfo.imageView = ssaoBlurH.imageView;
			ssaoBlurHInfo.sampler = VK_NULL_HANDLE;
			ssaoBlurHInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

			frameCtx.descriptorWriter.writePushImage(
				PUSH_SSAO_OUTPUT_TEX_BINDING,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				ssaoBlurHInfo
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
			frameCtx.descriptorWriter.clear();

			// Only have to adjust sampler and layout for blur H input
			ssaoBlurHInfo.sampler = samplerSSAO;
			ssaoBlurHInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			frameCtx.descriptorWriter.writePushImage(
				PUSH_SSAO_INPUT_TEX_BINDING,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				ssaoBlurHInfo
			);

			VkDescriptorImageInfo ssaoBlurVInfo{};
			ssaoBlurVInfo.imageView = ssaoBlurV.imageView;
			ssaoBlurVInfo.sampler = VK_NULL_HANDLE;
			ssaoBlurVInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

			frameCtx.descriptorWriter.writePushImage(
				PUSH_SSAO_OUTPUT_TEX_BINDING,
				VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				ssaoBlurVInfo
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
	}

	AttachmentDesc colorAttach{};
	if (msaaEnabled) {
		colorAttach.imageView = msaa.imageView;
		colorAttach.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
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
	depthAttach.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
	depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthAttach.clearValue.depthStencil.depth = 1.0f;

	RenderPasses::GraphicsRenderScope geometryScope;

	RenderPasses::beginRendering(
		frameCtx.commandBuffer,
		{ colorAttach, depthAttach },
		{ extent },
		geometryScope);

	// Sky box always occurs
	RenderPasses::skyboxPass(frameCtx, Pipelines::getHandle(PipelineID::Skybox), profiler);

	if (hasVisibles) {
		// Can always assume opaque draw
		PipelineHandle pipeline{};
		// Opaque pass
		if (!profiler.pipeOverride.enabled) {
			pipeline = Pipelines::getHandle(PipelineID::Opaque); // default mesh pipeline
		}
		else {
			pipeline = Pipelines::getHandle(profiler.pipeOverride.selectedID);
		}

		frameCtx.descriptorWriter.clear();
		if (frameCtx.descriptorWriter.enablePushDescriptor && profiler.debugToggles.enableSSAO) {
			VkDescriptorImageInfo ssaoFinalInfo{};
			ssaoFinalInfo.imageView = ssaoBlurV.imageView;
			ssaoFinalInfo.sampler = samplerSSAO;
			ssaoFinalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			frameCtx.descriptorWriter.writePushImage(
				PUSH_SSAO_INPUT_TEX_BINDING,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				ssaoFinalInfo
			);

			frameCtx.descriptorWriter.enablePushDescriptor = false;
		}
		else {
			// Pipeline needs a push layout so dummy white view and normal sampler
			auto& white = ResourceManager::getWhiteMat();
			VkDescriptorImageInfo dummyInfo{};
			dummyInfo.imageView = white.imageView;
			dummyInfo.sampler = samplerNormal;
			dummyInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

			frameCtx.descriptorWriter.writePushImage(
				PUSH_SSAO_INPUT_TEX_BINDING,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				dummyInfo
			);
		}

		RenderPasses::opaqueMeshPass(frameCtx, pipeline, profiler);

		// Transparent pass
		if (frameCtx.transparentRange.visibleCount > 0) {
			if (!profiler.pipeOverride.enabled) {
				pipeline = Pipelines::getHandle(PipelineID::Transparent);
			}

			RenderPasses::transparentMeshPass(frameCtx, pipeline, profiler);
		}

		if (profiler.debugToggles.showOBBs) {
			RenderPasses::obbDebugPass(frameCtx, Pipelines::getHandle(PipelineID::BoundingBox), profiler);
		}
	}

	RenderPasses::endRendering(frameCtx.commandBuffer);

	// ToneMapImage transition
	ImageUtils::transitionImage(
		frameCtx.commandBuffer,
		draw.image,
		draw.imageFormat,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL);

	ImageUtils::transitionImage(
		frameCtx.commandBuffer,
		toneMap.image,
		toneMap.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_GENERAL);

	// Tone mapping pass
	RenderPasses::ComputeDispatchScope tonemapPass;
	tonemapPass.setPush(&ResourceManager::toneMappingData);
	tonemapPass.extent = extent;
	tonemapPass.workgroupSize = workgroupSize;
	tonemapPass.calculateGroups();
	RenderPasses::dispatchComputePass(
		frameCtx,
		Pipelines::getHandle(PipelineID::ToneMap),
		tonemapPass);

	ImageUtils::transitionImage(
		frameCtx.commandBuffer,
		toneMap.image,
		toneMap.imageFormat,
		VK_IMAGE_LAYOUT_GENERAL,
		VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
	ImageUtils::transitionImage(
		frameCtx.commandBuffer,
		swp.images[frameCtx.swapchainImageIndex],
		draw.imageFormat,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

	ImageUtils::copyImageToImage(
		frameCtx.commandBuffer,
		toneMap.image,
		swp.images[frameCtx.swapchainImageIndex],
		{ _drawExtent.width, _drawExtent.height },
		swp.extent
	);

	const auto& debug = profiler.debugToggles;
	if (debug.enableSettings || debug.enableStats) {
		// Transition swapchain to COLOR_ATTACHMENT_OPTIMAL for ImGui
		ImageUtils::transitionImage(
			frameCtx.commandBuffer,
			swp.images[frameCtx.swapchainImageIndex],
			draw.imageFormat,
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
			draw.imageFormat,
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