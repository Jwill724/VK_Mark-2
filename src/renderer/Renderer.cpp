#include "pch.h"

#include "Renderer.h"
#include "engine/platform/profiler/EditorImgui.h"
#include "scene/RenderScene.h"
#include "utils/BufferUtils.h"
#include "utils/SyncUtils.h"
#include "renderer/passes/RenderPasses.h"
#include "backend/Backend.h"

void Renderer::Init()
{
	SyncUtils::CreateTimelineSemaphore(m_transferSync);

	if (Backend::IsComputeAvailable())
	{
		SyncUtils::CreateTimelineSemaphore(m_computeSync);
	}

	InitFrameResources();

	if (m_transformsStagingBuffer.IsValid())
	{
		m_transformsStagingBuffer = BufferUtils::CreateGPUStagingBuffer(
			MAX_INSTANCE_TRANSFORMS * sizeof(glm::mat4),
			m_allocator
		);
	}
}

void Renderer::InitFrameResources()
{
	auto& swapDef = Backend::GetSwapchainDef();
	m_framesInFlight = swapDef.imageCount;
	m_frameContexts.resize(m_framesInFlight);

	fmt::println("Frames in flight:[{}]", m_framesInFlight);

	uint32_t graphicsIndex = Backend::GetGraphicsQueue().GetFamilyIndex();
	uint32_t transferIndex = Backend::GetTransferQueue().GetFamilyIndex();
	uint32_t computeIndex = Backend::GetComputeQueue().GetFamilyIndex();

	size_t totalGPUStagingSize =
		MAX_GPU_INSTANCE_SIZE_BYTES +
		MAX_GPU_INDIRECT_SIZE_BYTES +
		sizeof(BindlessBufferTable);


}



// =============================================
// === CLEAR DATA AND AQUIRE SWAPCHAIN INDEX ===
void Renderer::PrepareFrame()
{
	auto device = Backend::GetDevice();
	auto& swapDef = Backend::GetSwapchainDef();

	auto& frameCtx = GetCurrentFrame();

	VkFence fence = swapDef.inFlightFences[frameCtx.m_frameIndex];
	VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
	VK_CHECK(vkResetFences(device, 1, &fence));

	if (Backend::QueueSupportsTimestamps(Backend::GetGraphicsQueue()) &&
		frameCtx.m_graphicsTimestampPool != VK_NULL_HANDLE &&
		frameCtx.m_bHasTimestampResultsPending)
	{
		const float timestampPeriod = Backend::GetTimestampPeriod();

		bool allReady = true;

		for (uint32_t passIndex = 1; passIndex < TIMESTAMP_PASS_COUNT; ++passIndex)
		{
			if (!frameCtx.m_timestampPassUsed[passIndex]) continue;

			const PassID passID = static_cast<PassID>(passIndex);
			const auto& range = frameCtx.m_passTimestampRanges[passIndex];

			uint64_t queryPair[2]{};

			VkResult res = vkGetQueryPoolResults(
				device,
				frameCtx.m_graphicsTimestampPool,
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

				Engine::GetProfiler().addGpuPassTime(passID, gpuMs);
			}

			frameCtx.m_timestampPassUsed[passIndex] = false;
		}

		// Frame timestamps
		{
			uint64_t queryPair[2]{};

			VkResult res = vkGetQueryPoolResults(
				device,
				frameCtx.m_graphicsTimestampPool,
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

					auto& stats = Engine::GetProfiler().getStats();
					stats.gpuFrameTimeRawMs = gpuMs;
					stats.gpuFrameTime.add(gpuMs);
				}
			}
		}

		frameCtx.m_bHasTimestampResultsPending = !allReady;
	}

	uint32_t imageIndex = 0;
	frameCtx.m_swapchainResult = vkAcquireNextImageKHR(
		device,
		swapDef.swapchain,
		UINT64_MAX,
		swapDef.imageAvailableSemaphores[frameCtx.m_frameIndex],
		VK_NULL_HANDLE,
		&imageIndex);

	if (frameCtx.m_swapchainResult == VK_ERROR_OUT_OF_DATE_KHR ||
		frameCtx.m_swapchainResult == VK_SUBOPTIMAL_KHR)
	{
		Backend::GetGraphicsQueue().WaitIdle();
		Backend::ResizeSwapchain();
		return;
	}
	ASSERT(frameCtx.m_swapchainResult == VK_SUCCESS && "Failed to acquire swapchain image!");

	frameCtx.m_swapchainImageIndex = imageIndex;

	// Mark image as in use
	swapDef.imageInFlightFrame[imageIndex] = frameCtx.m_frameIndex;

	VK_CHECK(vkResetCommandBuffer(frameCtx.m_commandBuffer, 0));

	frameCtx.FreeStashedCmds();
	frameCtx.m_gpuCopyStagingHead = 0;

	frameCtx.m_cpuDeletionQueue.Flush();

	const uint32_t curWidth = m_drawExtent.width;
	const uint32_t curHeight = m_drawExtent.height;

	if (frameCtx.m_cachedDrawExtentW != curWidth || frameCtx.m_cachedDrawExtentH != curHeight) {
		frameCtx.CreateClusterBuffers(
			curWidth,
			curHeight,
			m_allocator
		);
		frameCtx.CreateCMAA2Buffers(
			curWidth,
			curHeight,
			m_allocator
		);
		frameCtx.m_cachedDrawExtentW = curWidth;
		frameCtx.m_cachedDrawExtentH = curHeight;
	}

	// Handles initialization and any updates during runtime
	if (frameCtx.m_gpuAddressTable.IsTableDirty()) {
		frameCtx.m_gpuAddressTable.UpdateCpuVersion();
	}
}

// ===============================================
// === SYNC FRAME SEMAPHORES AND PRESENT FRAME ===
void Renderer::SubmitFrame()
{
	auto& frameCtx = GetCurrentFrame();

	VkSemaphore presentSem = m_swapchain.GetAvailableSemaphore();
	VkSemaphore renderSem  = m_swapchain.GetFinishedSemaphore();
	VkFence     fence      = m_swapchain.GetInFlightFence();

	std::vector<VkSemaphoreSubmitInfo> waitInfos;

	// Wait on image acquired semaphore
	VkSemaphoreSubmitInfo waitImageAvailable{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
	waitImageAvailable.semaphore = presentSem;
	waitImageAvailable.stageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	waitInfos.push_back(waitImageAvailable);

	// Wait on transfer timeline only up to the first buffer consumers
	if (frameCtx.transferWaitValue != UINT64_MAX && frameCtx.transferWaitValue <= m_transferSync.signalValue)
	{
		ASSERT(frameCtx.transferWaitValue <= m_transferSync.signalValue &&
			"Invalid transfer Wait: waiting on unsignaled or future timeline value!");

		VkSemaphoreSubmitInfo waitTransfer{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
		waitTransfer.semaphore = m_transferSync.semaphore;
		waitTransfer.value = frameCtx.transferWaitValue;
		waitTransfer.stageMask =
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT |
			VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT |
			VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT; // earliest consumers of uploaded data
		waitInfos.push_back(waitTransfer);
	}

	if (frameCtx.m_computeWaitValue != UINT64_MAX && frameCtx.m_computeWaitValue <= m_computeSync.signalValue)
	{
		ASSERT(frameCtx.m_computeWaitValue <= m_computeSync.signalValue &&
			"Invalid compute Wait: waiting on unsignaled or future timeline value!");

		VkSemaphoreSubmitInfo waitCompute{ VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO };
		waitCompute.semaphore = m_computeSync.semaphore;
		waitCompute.value = frameCtx.m_computeWaitValue;
		waitCompute.stageMask =
			VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT |
			VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
		waitInfos.push_back(waitCompute);

		fmt::println("Should only execute when used compute timeline");
	}

	VkCommandBufferSubmitInfo cmdInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO };
	cmdInfo.commandBuffer = frameCtx.m_commandBuffer;

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

	auto& gQueue = Backend::GetGraphicsQueue();
	auto& pQueue = Backend::GetPresentQueue();

	VK_CHECK(vkQueueSubmit2(gQueue.GetQueue(), 1, &submitInfo, fence));

	// Present using the image-indexed renderSem
	VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &renderSem;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &swapDef.swapchain;
	presentInfo.pImageIndices = &imageIndex;

	frameCtx.m_swapchainResult = vkQueuePresentKHR(pQueue.GetQueue(), &presentInfo);
	if (frameCtx.m_swapchainResult == VK_ERROR_OUT_OF_DATE_KHR || frameCtx.m_swapchainResult == VK_SUBOPTIMAL_KHR)
	{
		if (gQueue.GetQueue() != pQueue.GetQueue())
			pQueue.WaitIdle();
		else
			gQueue.WaitIdle();

		Backend::ResizeSwapchain();

		m_renderTargetDeletionQueue.Flush();
		// Recreate all render targets with extent updates
		ResourceManager::InitUniformRenderTargets(
			Backend::GetDevice(),
			m_renderTargetDeletionQueue,
			m_allocator,
			m_drawExtent);
	}
	else
	{
		ASSERT(frameCtx.m_swapchainResult == VK_SUCCESS && "Failed to present swapchain image!");
	}

	m_frameNumber++;
}

// Render Pass System V1
// Very simple linear recording
// ==============================================
// === MAIN GRAPHICS RECORDING FOR ALL PASSES ===
// ==============================================
// All recorded in a single graphics queue
void Renderer::RecordRenderCommand()
{
	const auto device = Backend::GetDevice();
	auto& swp = Backend::GetSwapchainDef();
	auto& opaque = ResourceManager::GetOpaque_Target();
	auto& transparentAccum = ResourceManager::GetTransparentAccumulation_Target();
	auto& transparentReveal = ResourceManager::GetTransparentRevealage_Target();
	auto& toneMap = ResourceManager::GetToneMap_Target();
	auto& aoRaw = ResourceManager::GetAORaw_Target();
	auto& bentNormals = ResourceManager::GetBentNormals_Target();
	auto& depthResolved = ResourceManager::GetDepthResolved_Target();
	auto& depth = ResourceManager::GetDepthRaw_Target();
	auto& volLight = ResourceManager::GetVolumetricLight_Target();
	auto& shadowMask = ResourceManager::GetScreenSpaceShadowMask_Target();
	const auto nearestClampSampler = ResourceManager::GetNearestClamp_Sampler();
	const auto linearClampSampler = ResourceManager::GetLinearClamp_Sampler();

	auto& frameCtx = GetCurrentFrame();

	auto& debug = profiler.debugToggles;

	const VkExtent2D fullExtent = { m_drawExtent.width, m_drawExtent.height };
	const VkExtent3D workgroupSize = { 16u, 16u, 1u };

	const auto& winExtent = Engine::GetWindowExtent();

	const auto unifiedSet = DescriptorSetOverwatch::GetUnifiedDescriptor().descriptorSet;
	const VkDescriptorSet sets[2]{
		unifiedSet,
		frameCtx.m_frameSet
	};

	auto& scene = RenderScene::getCurrentSceneData();

	bool hasVisibles = frameCtx.m_visibleCount > 0;
	bool isTemporalValid = !IsFirstFrame() && scene.temporal.y == 1;

	bool isGpuVersionValid = frameCtx.m_gpuAddressTable.IsVersionMismatched();
	if (!isGpuVersionValid)
	{
		ASSERT(!isGpuVersionValid && "GPU is about to use stale address table!"); // On debug build just kills program
		return; // Release mode early exit, honestly shouldn't ever occur
	}
	frameCtx.UpdateAddressTableIfDirty();
	frameCtx.WriteFrameUniforms();
	frameCtx.UpdateFrameSet();

	VkCommandBufferBeginInfo cmdBeginInfo{};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	VK_CHECK(vkBeginCommandBuffer(frameCtx.m_commandBuffer, &cmdBeginInfo));

	if (Backend::QueueSupportsTimestamps(Backend::GetGraphicsQueue()) && frameCtx.m_graphicsTimestampPool != VK_NULL_HANDLE)
	{
		if (!frameCtx.m_bHasTimestampResultsPending)
		{
			vkCmdResetQueryPool(
				frameCtx.m_commandBuffer,
				frameCtx.m_graphicsTimestampPool,
				0u,
				TIMESTAMP_QUERY_COUNT
			);

			frameCtx.m_timestampPassUsed.fill(false);
		}
	}

	if (Backend::QueueSupportsTimestamps(Backend::GetGraphicsQueue())) {
		vkCmdWriteTimestamp2(
			frameCtx.m_commandBuffer,
			VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
			frameCtx.m_graphicsTimestampPool,
			FRAME_BEGIN_QUERY
		);
	}

	// Note: Currently only do cpu culling, once its in a compute this would need to be done way before main recording
	if (frameCtx.m_bTransformsBufferUploadNeeded)
	{
		if (isTemporalValid)
			BarrierUtils::BufferTransferWriteToGraphicsRead(frameCtx.m_commandBuffer, frameCtx.m_prevTransforms_GPU);

		BarrierUtils::BufferTransferWriteToGraphicsRead(frameCtx.m_commandBuffer, frameCtx.m_transforms_GPU);
		frameCtx.m_bTransformsBufferUploadNeeded = false; // Should only m_frameSet back to false in here
	}

	if (frameCtx.m_bLightsBufferUploadNeeded)
	{
		// Compute always reads this first
		BarrierUtils::BufferComputeWriteToComputeRead(frameCtx.m_commandBuffer, frameCtx.m_lights_GPU);
		frameCtx.m_bLightsBufferUploadNeeded = false; // Should only m_frameSet back to false in here
	}

	if (hasVisibles)
	{
		BarrierUtils::BufferTransferWriteToGraphicsRead(frameCtx.m_commandBuffer, frameCtx.m_visibleInstances_GPU);
		BarrierUtils::BufferTransferWriteToIndirectRead(frameCtx.m_commandBuffer, frameCtx.m_indirectDraws_GPU);
	}

	vkCmdBindDescriptorSets(frameCtx.m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		Pipelines::m_globalLayout.layout, 0, 2, sets, 0, nullptr);

	vkCmdBindDescriptorSets(frameCtx.m_commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		Pipelines::m_globalLayout.layout, 0, 2, sets, 0, nullptr);

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		opaque,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

	// Used in opaque shading to determine if on
	if (hasVisibles) {
		// Always bind global index buffer at start of visibles frame
		const auto indexBuffer = gpuResources.GetGPUAddrsBuffer(BufferSlot::Index).m_buffer;
		vkCmdBindIndexBuffer(frameCtx.m_commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

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
			RenderPasses::HiZGenerationPass(frameCtx, hiZScope, profiler);
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

			RenderPasses::ClusterLightCullingPass(frameCtx, clusterScope, profiler);
		}

		glm::vec2 fullPixelSize = glm::vec2(scene.pixelSizes.x, scene.pixelSizes.y);
		glm::vec2 halfPixelSize = glm::vec2(scene.pixelSizes.z, scene.pixelSizes.w);

		// =================
		// === SSAO PASS ===
		if (debug.aoMode != AO_OFF) {
			auto& ssaoPush = profiler.ssaoSettings;

			const auto& proj = scene.proj;

			ssaoPush.depthLinearizeMult = -proj[3][2];
			ssaoPush.depthLinearizeAdd  =  proj[2][2];
			if (ssaoPush.depthLinearizeMult * ssaoPush.depthLinearizeAdd < 0.0) {
				ssaoPush.depthLinearizeAdd = -ssaoPush.depthLinearizeAdd;
			}

			ssaoPush.tanHalfFov.x = 1.0f / proj[0][0];
			ssaoPush.tanHalfFov.y = 1.0f / proj[1][1];

			ssaoPush.ndcToViewMul = { ssaoPush.tanHalfFov.x * 2.0f, ssaoPush.tanHalfFov.y * -2.0f };
			ssaoPush.ndcToViewAdd = { ssaoPush.tanHalfFov.x * -1.0f, ssaoPush.tanHalfFov.y * 1.0f };

			ssaoPush.ndcToViewMul_x_PixelSize = ssaoPush.ndcToViewMul * fullPixelSize;

			ssaoPush.noiseIndex = scene.temporal.x % 64u;
			ssaoPush.isFinalPass = 0u; // Reset each frame

			ssaoPush.blurDirection = { 1.0f, 0.0f }; // Horizontal first

			RenderPasses::ComputeScope ssaoScope;
			ssaoScope.passID = PassID::SSAO;
			ssaoScope.SetPush(ssaoPush);
			ssaoScope.extent = { aoRaw.extent.width, aoRaw.extent.height };
			ssaoScope.workgroupSize = workgroupSize;

			RenderPasses::SSAOPass(frameCtx, ssaoScope, profiler, isTemporalValid);
		}
		else {
			// AO OFF
			ImageUtils::transitionImage(
				frameCtx.m_commandBuffer,
				aoRaw,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			);

			ImageUtils::transitionImage(
				frameCtx.m_commandBuffer,
				bentNormals,
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
			);
		}

		if (debug.enableShadows) {
			// ================
			// === CSM PASS ===
			RenderPasses::GraphicsScope csmScope;
			csmScope.passID = PassID::DirectionalCSM;
			RenderPasses::DirectionalCSMPass(frameCtx, csmScope, profiler);


			// ==========================
			// === FLASH LIGHT SHADOW ===
			if (LightingSystem::_mainFlashLight.IsFlashLightOn()) {
				RenderPasses::GraphicsScope flScope;
				flScope.passID = PassID::FlashlightShadow;
				RenderPasses::ShadowFlashlightPass(
					frameCtx,
					flScope,
					profiler);
			}


			// =========================================
			// === SCREEN SPACE CONTACT SHADOWS PASS ===
			if (debug.enableSSS) {
				RenderPasses::ComputeScope sssScope;
				sssScope.passID = PassID::ScreenSpaceContactShadows;
				sssScope.bSkipGroups = true;
				sssScope.SetPush(profiler.contactShadowsSettings);
				RenderPasses::SSContactShadowsPass(frameCtx, sssScope, profiler);
			}
		}

		if (!debug.enableShadows || !debug.enableSSS) {
			ImageUtils::transitionImage(
				frameCtx.m_commandBuffer,
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
			frameCtx.m_commandBuffer,
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

	RenderPasses::BeginRendering(
		frameCtx.m_commandBuffer,
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
	RenderPasses::SkyboxPass(frameCtx, skyboxScope, profiler, hasVisibles);

	if (hasVisibles) {
		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_READ_1,
			aoRaw,
			nearestClampSampler
		);

		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_READ_2,
			shadowMask,
			nearestClampSampler
		);

		frameCtx.m_descriptorWriter.WritePushImage(
			PUSH_BINDING_READ_3,
			bentNormals,
			linearClampSampler
		);

		RenderPasses::OpaqueForwardPass(frameCtx, opaqueScope, profiler);

		// ======================
		// === OBB DEBUG PASS ===
		if (debug.enableOBBs) {
			RenderPasses::GraphicsScope obbScope;
			obbScope = opaqueScope;
			obbScope.passID = PassID::OBBLineView;
			RenderPasses::ObbLineDebugPass(frameCtx, obbScope, profiler);
		}
	}

	RenderPasses::EndRendering(frameCtx.m_commandBuffer);

	ImageUtils::transitionImage(
		frameCtx.m_commandBuffer,
		opaque,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// ========================
	// === TRANSPARENT PASS ===
	bool transparentVisible = false;
	if (frameCtx.m_transparentDrawRange.visibleCount > 0) {
		transparentVisible = true;

		ImageUtils::transitionImage(
			frameCtx.m_commandBuffer,
			transparentAccum,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
		ImageUtils::transitionImage(
			frameCtx.m_commandBuffer,
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

		RenderPasses::BeginRendering(
			frameCtx.m_commandBuffer,
			{
				tAccumAttach,
				tRevealAttach,
				depthAttach
			},
			{ fullExtent },
			transparentScope);

		RenderPasses::TransparentForwardPass(frameCtx, transparentScope, profiler);

		RenderPasses::EndRendering(frameCtx.m_commandBuffer);

		ImageUtils::transitionImage(
			frameCtx.m_commandBuffer,
			transparentAccum,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		ImageUtils::transitionImage(
			frameCtx.m_commandBuffer,
			transparentReveal,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		RenderPasses::ComputeScope tcScope;
		tcScope.passID = PassID::TransparentResolve;
		tcScope.extent = fullExtent;
		tcScope.workgroupSize = workgroupSize;
		RenderPasses::TransparentResolvePass(frameCtx, tcScope, profiler);
	}

	// ================================
	// === VOLUMETRIC LIGHTING PASS ===
	if (hasVisibles && debug.enableVolumetrics && debug.enableShadows) {
		glm::uvec2 halfRes{ volLight.extent.width, volLight.extent.height };

		auto& volLightPush = profiler.volLightSettings;

		RenderPasses::ComputeScope volLightScope;
		volLightScope.passID = PassID::VolumetricLighting;
		volLightScope.SetPush(volLightPush);
		volLightScope.extent = { halfRes.x, halfRes.y };
		volLightScope.workgroupSize = workgroupSize;

		RenderPasses::VolumetricLightingPass(frameCtx, volLightScope, profiler);
	}

	// ================
	// === TAA PASS ===
	if ((debug.aaMode == AA_TAA) && hasVisibles && isTemporalValid) {
		RenderPasses::ComputeScope taaScope;
		taaScope.passID = PassID::TAA;
		taaScope.workgroupSize = workgroupSize;
		taaScope.extent = fullExtent;
		taaScope.SetPush(profiler.taaSettings);

		RenderPasses::TAAPass(frameCtx, taaScope, profiler);
	}

	// =====================
	// === EXPOSURE PASS ===
	RenderPasses::ComputeScope exposureScope;
	exposureScope.passID = PassID::Exposure;
	exposureScope.extent = fullExtent;
	exposureScope.workgroupSize = workgroupSize;
	const auto& luminanceBuf = gpuResources.GetGPUAddrsBuffer(BufferSlot::Luminance);
	RenderPasses::ExposurePass(
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
		const auto& brightFlare = ResourceManager::GetFlareBright_Target();
		VkExtent2D quarterRes = {
			brightFlare.extent.width,
			brightFlare.extent.height
		};

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
		lensFlareScope.SetPush(lensFlarePush);
		lensFlareScope.extent = quarterRes;
		lensFlareScope.workgroupSize = workgroupSize;

		RenderPasses::LensFlarePass(frameCtx,
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
	RenderPasses::FinalCompositePass(frameCtx,
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
				cmaa2Scope.SetPush(frameCtx.m_cmaa2Push);

				const uint32_t quadCountX = (fullExtent.width + 1u) >> 1u;
				const uint32_t quadCountY = (fullExtent.height + 1u) >> 1u;
				const uint32_t groupsX = (quadCountX + 13u) / 14u;
				const uint32_t groupsY = (quadCountY + 13u) / 14u;
				cmaa2Scope.groupCountX = groupsX;
				cmaa2Scope.groupCountY = groupsY;
				cmaa2Scope.bSkipGroups = true;
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

				smaaScope.SetPush(gpuResources.smaaTextures);

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
		auto& aaColor = ResourceManager::GetAAColor_Target();
		ImageUtils::imageCopy(
			frameCtx.m_commandBuffer,
			(copyPostAAImage) ? aaColor : toneMap,
			swp.images[frameCtx.m_swapchainImageIndex],
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
		RenderPasses::ChromaticAberrationPass(
			frameCtx,
			caScope,
			profiler,
			hasVisibles
		);

		ImageUtils::imageCopy(
			frameCtx.m_commandBuffer,
			ResourceManager::GetPostNonAAComposite_Target(),
			swp.images[frameCtx.m_swapchainImageIndex],
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			false);
	}

	if (debug.enableSettings || debug.enableProfilerView) {
		ImageUtils::transitionImage(
			frameCtx.m_commandBuffer,
			swp.images[frameCtx.m_swapchainImageIndex],
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		EditorImgui::drawImgui(
			frameCtx.m_commandBuffer,
			swp.images[frameCtx.m_swapchainImageIndex].imageView,
			swp.extent,
			false);

		ImageUtils::transitionImage(
			frameCtx.m_commandBuffer,
			swp.images[frameCtx.m_swapchainImageIndex],
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	}

	if (Backend::QueueSupportsTimestamps(Backend::GetGraphicsQueue())) {
		vkCmdWriteTimestamp2(
			frameCtx.m_commandBuffer,
			VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
			frameCtx.m_graphicsTimestampPool,
			FRAME_END_QUERY
		);
	}

	profiler.collectTracyGPU(frameCtx.m_commandBuffer);

	if (Backend::QueueSupportsTimestamps(Backend::GetGraphicsQueue()) &&
		frameCtx.m_graphicsTimestampPool != VK_NULL_HANDLE)
	{
		frameCtx.m_bHasTimestampResultsPending = true;
	}

	VK_CHECK(vkEndCommandBuffer(frameCtx.m_commandBuffer));
}


void Renderer::Cleanup()
{
	CleanupFrameContexts(m_frameContexts, device, alloc);

	if (_transferSync.semaphore != VK_NULL_HANDLE)
		vkDestroySemaphore(device, _transferSync.semaphore, nullptr);

	if (_computeSync.semaphore != VK_NULL_HANDLE)
		vkDestroySemaphore(device, _computeSync.semaphore, nullptr);
}
