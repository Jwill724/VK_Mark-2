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
	const uint32_t& getFrameNumber() { return _frameNumber; }
	uint32_t _framesInFlight{ 0 };

	const bool isFirstFrame() { return _frameNumber == 0; }

	std::mutex _frameAccessMutex;

	FrameContext& getCurrentFrame() {
		std::scoped_lock lock(_frameAccessMutex);
		return *_frameContexts[_frameNumber % _framesInFlight];
	}

	FrameContext& getLastFrame() {
		std::scoped_lock lock(_frameAccessMutex);
		const uint32_t lastFrameNumber = _frameNumber + _framesInFlight - 1u;
		ASSERT(lastFrameNumber >= 0);
		return *_frameContexts[lastFrameNumber % _framesInFlight];
	}

	TimelineSync _transferSync;
	TimelineSync _computeSync;

	ForwardPush _forwardPush;
	ForwardPush& getForwardPush() { return _forwardPush; }
}

void Renderer::initRenderer(
	const VkDevice device,
	const VkDescriptorSetLayout frameLayout,
	GPUResources& gpuResources,
	Profiler& profiler)
{
	SyncUtils::createTimelineSemaphore(_transferSync, device);

	if (Backend::isComputeAvailable()) {
		SyncUtils::createTimelineSemaphore(_computeSync, device);
	}

	const auto alloc = gpuResources.getAllocator();

	_frameContexts = initFrameContexts(
		device,
		frameLayout,
		alloc,
		_framesInFlight
	);

	auto& debug = profiler.debugToggles;
	const auto& modelDataCounts = gpuResources.modelDataCounts;

	debug.indexCount = modelDataCounts.totalIndexCount;
	debug.vertexCount = modelDataCounts.totalVertexCount;
	debug.materialCount = modelDataCounts.totalMaterialCount;
	debug.transformCount = modelDataCounts.totalTransformCount;
	debug.meshCount = modelDataCounts.totalMeshCount;


	auto& transformStaging = gpuResources.getInstanceTransformsStagingBuffer();

	if (transformStaging.buffer == VK_NULL_HANDLE) {
		constexpr size_t TRANSFORM_STAGING_BYTES = MAX_INSTANCE_TRANSFORMS * sizeof(glm::mat4);
		transformStaging = BufferUtils::createBuffer(
			TRANSFORM_STAGING_BYTES,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
			alloc
		);
		ASSERT(transformStaging.info.pMappedData);
	}
}

// =============================================
// === CLEAR DATA AND AQUIRE SWAPCHAIN INDEX ===
void Renderer::prepareFrameContext(FrameContext& frameCtx, const VmaAllocator alloc) {
	auto device = Backend::getDevice();
	auto& swapDef = Backend::getSwapchainDef();

	VkFence fence = swapDef.inFlightFences[frameCtx.frameIndex];
	VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
	VK_CHECK(vkResetFences(device, 1, &fence));

	if (Backend::queueSupportsTimestamps(Backend::getGraphicsQueue()) &&
		frameCtx.graphicsTimestampPool != VK_NULL_HANDLE &&
		frameCtx.hasTimestampResultsPending)
	{
		const float timestampPeriod = Backend::getTimestampPeriod();

		bool allReady = true;

		for (uint32_t passIndex = 1; passIndex < TIMESTAMP_PASS_COUNT; ++passIndex)
		{
			if (!frameCtx.timestampPassUsed[passIndex]) continue;

			const PassID passID = static_cast<PassID>(passIndex);
			const auto& range = frameCtx.passTimestampRanges[passIndex];

			uint64_t queryPair[2]{};

			VkResult res = vkGetQueryPoolResults(
				device,
				frameCtx.graphicsTimestampPool,
				range.beginQuery,
				2,
				sizeof(queryPair),
				queryPair,
				sizeof(uint64_t),
				VK_QUERY_RESULT_64_BIT
			);

			if (res == VK_NOT_READY) {
				allReady = false;
				continue;
			}

			VK_CHECK(res);

			const uint64_t beginTicks = queryPair[0];
			const uint64_t endTicks   = queryPair[1];

			if (endTicks > beginTicks)
			{
				const uint64_t deltaTicks = endTicks - beginTicks;

				const float gpuMs = static_cast<float>(
					(double(deltaTicks) * double(timestampPeriod)) / 1000000.0
				);

				Engine::getProfiler().addGpuPassTime(passID, gpuMs);
			}

			frameCtx.timestampPassUsed[passIndex] = false;
		}

		// Frame timestamps
		{
			uint64_t queryPair[2]{};

			VkResult res = vkGetQueryPoolResults(
				device,
				frameCtx.graphicsTimestampPool,
				FRAME_BEGIN_QUERY,
				2,
				sizeof(queryPair),
				queryPair,
				sizeof(uint64_t),
				VK_QUERY_RESULT_64_BIT
			);

			if (res == VK_NOT_READY) {
				allReady = false;
			}
			else {
				VK_CHECK(res);

				const uint64_t delta = queryPair[1] - queryPair[0];

				if (queryPair[1] > queryPair[0])
				{
					const float gpuMs = static_cast<float>(
						(double(delta) * double(timestampPeriod)) / 1000000.0
					);

					auto& stats = Engine::getProfiler().getStats();
					stats.gpuFrameTimeRawMs = gpuMs;
					stats.gpuFrameTime.add(gpuMs);
				}
			}
		}

		frameCtx.hasTimestampResultsPending = !allReady;
	}

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

	const uint32_t curWidth = _drawExtent.width;
	const uint32_t curHeight = _drawExtent.height;

	if (frameCtx.cachedExtentWidth != curWidth || frameCtx.cachedExtentHeight != curHeight) {
		frameCtx.createClusterBuffers(
			curWidth,
			curHeight,
			alloc
		);
		frameCtx.createCMAA2Buffers(
			curWidth,
			curHeight,
			alloc
		);
		frameCtx.cachedExtentWidth = curWidth;
		frameCtx.cachedExtentHeight = curHeight;
	}

	if (frameCtx.addressTable.isTableDirty()) {
		frameCtx.addressTable.updateCpuVersion();
	}
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
	if (frameCtx.transferWaitValue != UINT64_MAX && frameCtx.transferWaitValue <= _transferSync.signalValue)
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

	if (frameCtx.computeWaitValue != UINT64_MAX && frameCtx.computeWaitValue <= _computeSync.signalValue)
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

		fmt::println("Should only execute when used compute timeline");
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

		auto& targetQ1 = resources.getRenderTargetDQueue();
		targetQ1.flush();
		// Recreate all render targets with extent updates
		ResourceManager::initUniformRenderTargets(
			Backend::getDevice(),
			targetQ1,
			resources.getAllocator(),
			_drawExtent);
	}
	else {
		ASSERT(frameCtx.swapchainResult == VK_SUCCESS && "Failed to present swapchain image!");
	}

	_frameNumber++;
}

// Render Pass System V1
// Very simple linear recording
// ==============================================
// === MAIN GRAPHICS RECORDING FOR ALL PASSES ===
// ==============================================
// All recorded in a single graphics queue
void Renderer::recordRenderCommand(FrameContext& frameCtx, Profiler& profiler) {
	const auto device = Backend::getDevice();
	auto& swp = Backend::getSwapchainDef();
	auto& opaque = ResourceManager::getOpaque_Target();
	auto& transparentAccum = ResourceManager::getTransparentAccumulation_Target();
	auto& transparentReveal = ResourceManager::getTransparentRevealage_Target();
	auto& toneMap = ResourceManager::getToneMap_Target();
	auto& aoRaw = ResourceManager::getAORaw_Target();
	auto& depthResolved = ResourceManager::getDepthResolved_Target();
	auto& depth = ResourceManager::getDepthRaw_Target();
	auto& volLight = ResourceManager::getVolumetricLight_Target();
	auto& shadowMask = ResourceManager::getScreenSpaceShadowMask_Target();
	const auto aoSampler = ResourceManager::getLinearLODClamp_Sampler();
	const auto linearClampSampler = ResourceManager::getLinearClamp_Sampler();

	auto& debug = profiler.debugToggles;

	const VkExtent2D fullExtent = { _drawExtent.width, _drawExtent.height };
	const VkExtent3D workgroupSize = { 16u, 16u, 1u };

	const auto& winExtent = Engine::getWindowExtent();

	const auto unifiedSet = DescriptorSetOverwatch::getUnifiedDescriptor().descriptorSet;
	const VkDescriptorSet sets[2]{
		unifiedSet,
		frameCtx.set
	};

	auto& gpuResources = Engine::getState().getGPUResources();

	auto& scene = RenderScene::getCurrentSceneData();

	bool hasVisibles = frameCtx.visibleCount > 0;
	bool isTemporalValid = (!isFirstFrame() && scene.temporal.y == 1);

	ASSERT(!(frameCtx.addressTable.versionMismatch()) && "GPU is about to use stale address table!");
	frameCtx.updateAddressTableIfDirty(device);
	frameCtx.writeFrameUniforms(device);
	frameCtx.updateFrameSet(device);

	VkCommandBufferBeginInfo cmdBeginInfo{};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(frameCtx.cmdBuffer, &cmdBeginInfo));

	if (Backend::queueSupportsTimestamps(Backend::getGraphicsQueue()) &&
		frameCtx.graphicsTimestampPool != VK_NULL_HANDLE) {
		if (!frameCtx.hasTimestampResultsPending) {
			vkCmdResetQueryPool(
				frameCtx.cmdBuffer,
				frameCtx.graphicsTimestampPool,
				0u,
				TIMESTAMP_QUERY_COUNT
			);

			frameCtx.timestampPassUsed.fill(false);
		}
	}

	if (Backend::queueSupportsTimestamps(Backend::getGraphicsQueue())) {
		vkCmdWriteTimestamp2(
			frameCtx.cmdBuffer,
			VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
			frameCtx.graphicsTimestampPool,
			FRAME_BEGIN_QUERY
		);
	}

	// Note: Currently only do cpu culling, once its in a compute this would need to be done way before main recording
	if (frameCtx.transformsBufferUploadNeeded) {
		if (isTemporalValid) {
			BarrierUtils::bufferTransferWriteToGraphicsRead(frameCtx.cmdBuffer, frameCtx.prevTransforms_GPU);
		}

		BarrierUtils::bufferTransferWriteToGraphicsRead(frameCtx.cmdBuffer, frameCtx.transforms_GPU);
		frameCtx.transformsBufferUploadNeeded = false; // Should only set back to false in here
	}

	if (frameCtx.lightsBufferUploadNeeded) {
		// Compute always reads this first
		BarrierUtils::bufferComputeWriteToComputeRead(frameCtx.cmdBuffer, frameCtx.lights_GPU);
		frameCtx.lightsBufferUploadNeeded = false; // Should only set back to false in here
	}

	if (hasVisibles) {
		BarrierUtils::bufferTransferWriteToGraphicsRead(frameCtx.cmdBuffer, frameCtx.visibleInstances_GPU);
		BarrierUtils::bufferTransferWriteToIndirectRead(frameCtx.cmdBuffer, frameCtx.indirectDraws_GPU);
	}

	vkCmdBindDescriptorSets(frameCtx.cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		Pipelines::_globalLayout.layout, 0, 2, sets, 0, nullptr);

	vkCmdBindDescriptorSets(frameCtx.cmdBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		Pipelines::_globalLayout.layout, 0, 2, sets, 0, nullptr);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		opaque,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	// Used in opaque shading to determine if on
	if (hasVisibles) {
		// Always bind global index buffer at start of visibles frame
		const auto indexBuffer = gpuResources.getGPUAddrsBuffer(AddressBufferType::Index).buffer;
		vkCmdBindIndexBuffer(frameCtx.cmdBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

		// ===============
		// === PREPASS ===
		{
			RenderPasses::GraphicsScope basePrePassScope;
			basePrePassScope.passID = PassID::Prepass;
			RenderPasses::BasePrepass(frameCtx, basePrePassScope, profiler, isTemporalValid);
		}

		// ============================
		// === HI-Z GENERATION PASS ===
		{
			RenderPasses::ComputeScope hiZScope;
			hiZScope.passID = PassID::HiZGeneration;
			RenderPasses::hiZGenerationPass(frameCtx, hiZScope, profiler);
		}

		// ==================================
		// === CLUSTERED LIGHT BUILD PASS ===
		if (LightingSystem::getActiveLightCount() > 0) {
			RenderPasses::ComputeScope clusterScope;
			clusterScope.passID = PassID::ClusteredLightBuild;
			clusterScope.extent = {
				LightingSystem::_clusteredData.tileCountX,
				LightingSystem::_clusteredData.tileCountY
			};
			clusterScope.workgroupSize = { 8u, 8u, 1u }; // Initial tile size

			RenderPasses::clusteredPass(frameCtx, clusterScope, profiler);
		}

		glm::vec2 fullPixelSize = glm::vec2(scene.pixelSizes.x, scene.pixelSizes.y);
		glm::vec2 halfPixelSize = glm::vec2(scene.pixelSizes.z, scene.pixelSizes.w);

		// =================
		// === GTAO PASS ===
		if (debug.aoMode == AO_GTAO) {
			auto& gtaoPush = profiler.gtaoSettings;

			const auto& proj = scene.proj;

			gtaoPush.tanHalfFov.x = 1.0f / proj[0][0];
			gtaoPush.tanHalfFov.y = 1.0f / proj[1][1];

			gtaoPush.ndcToViewMul = { gtaoPush.tanHalfFov.x * 2.0f, gtaoPush.tanHalfFov.y * -2.0f };
			gtaoPush.ndcToViewAdd = { gtaoPush.tanHalfFov.x * -1.0f, gtaoPush.tanHalfFov.y * 1.0f };

			gtaoPush.ndcToViewMul_x_PixelSize = gtaoPush.ndcToViewMul * fullPixelSize;

			RenderPasses::ComputeScope gtaoScope;
			gtaoScope.passID = PassID::GTAO;
			gtaoScope.setPush(gtaoPush);
			gtaoScope.extent = { aoRaw.extent.width, aoRaw.extent.height };
			gtaoScope.workgroupSize = workgroupSize;

			RenderPasses::GTAOPass(frameCtx, gtaoScope, profiler, isTemporalValid);
		}
		else {
			// AO OFF
			ImageUtils::transitionImage(
				frameCtx.cmdBuffer,
				aoRaw,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			);
		}

		if (debug.enableShadows) {
			// ================
			// === CSM PASS ===
			RenderPasses::GraphicsScope csmScope;
			csmScope.passID = PassID::DirectionalCSM;
			RenderPasses::shadowCSMPass(frameCtx, csmScope, profiler);


			// ==========================
			// === FLASH LIGHT SHADOW ===
			if (LightingSystem::_mainFlashLight.isFlashLightOn()) {
				RenderPasses::GraphicsScope flScope;
				flScope.passID = PassID::FlashlightShadow;
				RenderPasses::shadowFlashLightPass(
					frameCtx,
					flScope,
					profiler);
			}


			// =========================================
			// === SCREEN SPACE CONTACT SHADOWS PASS ===
			if (debug.enableSSS) {
				RenderPasses::ComputeScope sssScope;
				sssScope.passID = PassID::ScreenSpaceContactShadows;
				sssScope.skipGroups = true;
				sssScope.setPush(profiler.contactShadowsSettings);
				RenderPasses::screenSpaceContactShadowsPass(frameCtx, sssScope, profiler);
			}
		}

		if (!debug.enableShadows || !debug.enableSSS) {
			ImageUtils::transitionImage(
				frameCtx.cmdBuffer,
				shadowMask,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			);
		}
	}


	// =================================
	// === MAIN FORWARD SHADING PASS ===
	AttachmentDesc opaqueAttach{};
	opaqueAttach.imageView = opaque.imageView;
	opaqueAttach.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	AttachmentDesc depthAttach{};
	depthAttach.clearValue.depthStencil.depth = 0.0f;

	// Default depth read from pre pass
	depthAttach.imageView = depthResolved.imageView;
	depthAttach.layout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL;
	depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	// Depth write
	// Transparent needs the resolved
	if ((profiler.pipeOverride.enabled) || // Note: Right now this only works due to only wireframe being an override pipeline
		!hasVisibles)
	{
		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			depth,
			VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

		depthAttach.imageView = depth.imageView;
		depthAttach.layout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		depthAttach.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttach.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	}

	RenderPasses::GraphicsScope opaqueScope;
	opaqueScope.passID = PassID::OpaqueForward;
	// Define push constant for forward rendering passes opaque and transparent
	_forwardPush.activeLightCount = LightingSystem::getActiveLightCount();
	_forwardPush.flashlightVP = LightingSystem::_mainFlashLight.viewProj;

	RenderPasses::beginRendering(
		frameCtx.cmdBuffer,
		{
			opaqueAttach,
			depthAttach
		},
		{ fullExtent },
		opaqueScope);

	// ===================
	// === SKYBOX PASS ===
	// Sky box draw always occurs
	RenderPasses::GraphicsScope skyboxScope;
	skyboxScope = opaqueScope;
	skyboxScope.passID = PassID::Skybox;
	RenderPasses::skyboxPass(frameCtx, skyboxScope, profiler, hasVisibles);

	if (hasVisibles) {
		// Final ao output
		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_READ_1,
			aoRaw,
			aoSampler
		);

		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_READ_2,
			shadowMask,
			ResourceManager::getNearestClamp_Sampler()
		);

		AllocatedImage bentNormals;
		if (debug.aoMode == AO_GTAO) {
			bentNormals = ResourceManager::getBentNormals_Target();
		}
		else {
			// pointless write
			bentNormals = aoRaw;
		}

		frameCtx.descriptorWriter.writePushImage(
			PUSH_BINDING_READ_3,
			bentNormals,
			linearClampSampler
		);

		RenderPasses::opaqueMeshPass(frameCtx, opaqueScope, profiler);

		// ======================
		// === OBB DEBUG PASS ===
		if (debug.enableOBBs) {
			RenderPasses::GraphicsScope obbScope;
			obbScope = opaqueScope;
			obbScope.passID = PassID::OBBLineView;
			RenderPasses::obbLinePass(frameCtx, obbScope, profiler);
		}
	}

	RenderPasses::endRendering(frameCtx.cmdBuffer);

	ImageUtils::transitionImage(
		frameCtx.cmdBuffer,
		opaque,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// ========================
	// === TRANSPARENT PASS ===
	bool transparentVisible = false;
	if (frameCtx.transparentRange.visibleCount > 0) {
		transparentVisible = true;

		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			transparentAccum,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			transparentReveal,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);


		AttachmentDesc tAccumAttach{};
		tAccumAttach.imageView = transparentAccum.imageView;
		tAccumAttach.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		tAccumAttach.clearValue.color = { 0.0f, 0.0f, 0.0f, 0.0f };

		AttachmentDesc tRevealAttach{};
		tRevealAttach.imageView = transparentReveal.imageView;
		tRevealAttach.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		tRevealAttach.clearValue.color = { 1.0f, 0.0f, 0.0f, 0.0f };

		RenderPasses::GraphicsScope transparentScope;
		transparentScope.passID = PassID::TransparentForward;

		RenderPasses::beginRendering(
			frameCtx.cmdBuffer,
			{
				tAccumAttach,
				tRevealAttach,
				depthAttach
			},
			{ fullExtent },
			transparentScope);

		RenderPasses::transparentMeshPass(frameCtx, transparentScope, profiler);

		RenderPasses::endRendering(frameCtx.cmdBuffer);

		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			transparentAccum,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			transparentReveal,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		RenderPasses::ComputeScope tcScope;
		tcScope.passID = PassID::TransparentResolve;
		tcScope.extent = fullExtent;
		tcScope.workgroupSize = workgroupSize;
		RenderPasses::transparentResolvePass(frameCtx, tcScope, profiler);
	}

	// ================================
	// === VOLUMETRIC LIGHTING PASS ===
	if (hasVisibles && debug.enableVolumetrics && debug.enableShadows) {
		glm::uvec2 halfRes{ volLight.extent.width, volLight.extent.height };

		auto& volLightPush = profiler.volLightSettings;
		volLightPush.pixelSize = {
			1.0f / static_cast<float>(halfRes.x),
			1.0f / static_cast<float>(halfRes.y)
		};

		RenderPasses::ComputeScope volLightScope;
		volLightScope.passID = PassID::VolumetricLighting;
		volLightScope.setPush(volLightPush);
		volLightScope.extent = { halfRes.x, halfRes.y };
		volLightScope.workgroupSize = workgroupSize;

		RenderPasses::volumetricLightingPass(frameCtx, volLightScope, profiler);
	}

	// ================
	// === TAA PASS ===
	if ((debug.aaMode == AA_TAA) && hasVisibles && isTemporalValid) {
		RenderPasses::ComputeScope taaScope;
		taaScope.passID = PassID::TAA;
		taaScope.workgroupSize = workgroupSize;
		taaScope.extent = fullExtent;
		taaScope.setPush(profiler.taaSettings);

		RenderPasses::TAAPass(frameCtx, taaScope, profiler);
	}

	// =====================
	// === EXPOSURE PASS ===
	RenderPasses::ComputeScope exposureScope;
	exposureScope.passID = PassID::Exposure;
	exposureScope.extent = fullExtent;
	exposureScope.workgroupSize = workgroupSize;
	const auto& luminanceBuf = gpuResources.getGPUAddrsBuffer(AddressBufferType::Luminance);
	RenderPasses::exposurePass(
		frameCtx,
		exposureScope,
		profiler,
		luminanceBuf,
		transparentVisible,
		hasVisibles,
		isTemporalValid);

	// =======================
	// === LENS FLARE PASS ===
	if (debug.enableLensFlare && hasVisibles) {
		auto& lensFlarePush = profiler.lensFlareSettings;
		const auto& brightFlare = ResourceManager::getFlareBright_Target();
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

		RenderPasses::ComputeScope lensFlareScope;
		lensFlareScope.passID = PassID::LensFlare;
		lensFlareScope.setPush(lensFlarePush);
		lensFlareScope.extent = quarterRes;
		lensFlareScope.workgroupSize = workgroupSize;

		RenderPasses::lensFlarePass(frameCtx,
			lensFlareScope,
			profiler,
			transparentVisible,
			hasVisibles,
			isTemporalValid
		);
	}


	// ============================
	// === FINAL COMPOSITE PASS ===
	exposureScope.passID = PassID::FinalComposite;
	RenderPasses::finalCompositePass(frameCtx,
		exposureScope,
		profiler,
		transparentVisible,
		hasVisibles,
		isTemporalValid);

	bool copyPostAAImage = false;
	if (hasVisibles && (debug.aaMode != AA_OFF && debug.aaMode != AA_TAA)) {
		switch(debug.aaMode)
		{
			// ==================
			// === CMAA2 PASS ===
			case AA_CMAA2:
			{
				RenderPasses::ComputeScope cmaa2Scope;
				cmaa2Scope.passID = PassID::CMAA2;
				cmaa2Scope.setPush(frameCtx.cmaa2Push);

				const uint32_t quadCountX = (fullExtent.width + 1u) >> 1u;
				const uint32_t quadCountY = (fullExtent.height + 1u) >> 1u;
				const uint32_t groupsX = (quadCountX + 13u) / 14u;
				const uint32_t groupsY = (quadCountY + 13u) / 14u;
				cmaa2Scope.groupCountX = groupsX;
				cmaa2Scope.groupCountY = groupsY;
				cmaa2Scope.skipGroups = true;
				cmaa2Scope.extent = { groupsX * 16u, groupsY * 16u };
				RenderPasses::CMAA2Pass(frameCtx, cmaa2Scope, profiler);
				copyPostAAImage = true;
				break;
			}

			// =================
			// === SMAA PASS ===
			case AA_SMAA:
			{
				RenderPasses::ComputeScope smaaScope;
				smaaScope.passID = PassID::SMAA;
				smaaScope.workgroupSize = workgroupSize;
				smaaScope.extent = fullExtent;

				smaaScope.setPush(gpuResources.smaaTextures);

				RenderPasses::SMAAPass(frameCtx, smaaScope, profiler);
				copyPostAAImage = true;
				break;
			}

			// =================
			// === FXAA PASS ===
			case AA_FXAA:
			{
				RenderPasses::ComputeScope fxaaScope;
				fxaaScope.passID = PassID::FXAA;
				fxaaScope.workgroupSize = workgroupSize;
				fxaaScope.extent = fullExtent;

				RenderPasses::FXAAPass(frameCtx, fxaaScope, profiler);
				copyPostAAImage = true;
				break;
			}
		}
	}

	if (!debug.enableChromaticAberration) {
		auto& aaColor = ResourceManager::getAAColor_Target();
		ImageUtils::imageCopy(
			frameCtx.cmdBuffer,
			(copyPostAAImage) ? aaColor : toneMap,
			swp.images[frameCtx.swapchainImageIndex],
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			false);
	}
	// =================================
	// === CHROMATIC ABERRATION PASS ===
	else {
		RenderPasses::ComputeScope caScope;
		caScope.passID = PassID::ChromaticAberration;
		caScope.extent = fullExtent;
		caScope.workgroupSize = workgroupSize;
		RenderPasses::chromaticAberrationPass(
			frameCtx,
			caScope,
			profiler,
			hasVisibles
		);

		ImageUtils::imageCopy(
			frameCtx.cmdBuffer,
			ResourceManager::getPostNonAAComposite_Target(),
			swp.images[frameCtx.swapchainImageIndex],
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			false);
	}


	if (debug.enableSettings || debug.enableProfilerView) {
		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			swp.images[frameCtx.swapchainImageIndex],
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		EditorImgui::drawImgui(
			frameCtx.cmdBuffer,
			swp.images[frameCtx.swapchainImageIndex].imageView,
			swp.extent,
			false);

		ImageUtils::transitionImage(
			frameCtx.cmdBuffer,
			swp.images[frameCtx.swapchainImageIndex],
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	}

	if (Backend::queueSupportsTimestamps(Backend::getGraphicsQueue())) {
		vkCmdWriteTimestamp2(
			frameCtx.cmdBuffer,
			VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
			frameCtx.graphicsTimestampPool,
			FRAME_END_QUERY
		);
	}

	profiler.collectTracyGPU(frameCtx.cmdBuffer);

	if (Backend::queueSupportsTimestamps(Backend::getGraphicsQueue()) &&
		frameCtx.graphicsTimestampPool != VK_NULL_HANDLE)
	{
		frameCtx.hasTimestampResultsPending = true;
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
