#include "pch.h"

#include "DrawPreparation.h"
#include "engine/Engine.h"
#include "utils/BufferUtils.h"

struct CombinedUploadPlan {
	// frame-local
	size_t visOff = 0, visSize = 0; // visible instances
	size_t indOff = 0, indSize = 0; // indirect draws

	// Only required when visibles and draw buffers are created or destroyed.
	// per-frame address table (frameCtx.addressTable_GPU)
	size_t fAddrOff = 0, fAddrSize = 0;
};

struct TransparentEntry {
	uint32_t instanceIndex;
	float distSq;
};

// Maintain high quality meshes within acceptable distances to maintain stability
static uint32_t getShadowMeshIDForCascade(
	const MeshLODs& lods,
	uint32_t cascadeIndex)
{
	if ((lods.flags & MESH_LOD_FLAG_FORCE_SHADOW_LOD0) != 0u) return lods.shadowLod0;

	if (cascadeIndex == 0 || cascadeIndex == 1) return lods.shadowLod0;
	if (cascadeIndex == 2) return lods.shadowLod1;
	return lods.shadowLod2;
}

static uint32_t shadowSlotToMeshID(const MeshLODs& lods, uint32_t slot)
{
	if (slot == 0u) return lods.shadowLod0;
	if (slot == 1u) return lods.shadowLod1;
	return lods.shadowLod2;
}

static uint32_t applyFoliageBias(uint32_t baseSlot, uint32_t cascadeIndex) {
	if (cascadeIndex == 0u) return 0u; // force best

	if (cascadeIndex == 1u) {
		// 1 step more detailed than the base rule
		if (baseSlot > 0u) return baseSlot - 1u;
		return 0u;
	}

	return baseSlot;
}

// All render data for FrameContext is reset prior to this function.
// When inputs are entered, visibleInstances and worldAABBS are be 1:1 in access before batched/sorted.
//
// Goal is to create a mega indirect draw buffer with takes many paths.
// Many instance paths are stored in their own containers but the main visible set
// visibleInstances is filled with opaque and transparent and here all other paths are built
// to append to the visible instances buffer.
void DrawPreparation::buildAndSortIndirectDraws(
	FrameContext& frameCtx,
	const std::vector<GPUMeshData>& meshes,
	const std::vector<MeshLODs>& meshLods,
	const std::vector<AABB>& worldAABBs,
	const glm::vec4& cameraPos,
	const glm::mat4& cameraProj,
	const DebugToggles& dbg)
{
	std::vector<GPUInstance> visibleInstances;

	visibleInstances.reserve(frameCtx.visibleInstances.size());

	ASSERT(frameCtx.visibleInstances.size() == worldAABBs.size() && "Visible Instances should be 1:1 with world AABBs.");

	const glm::vec3 camPos = glm::vec3(cameraPos);

	// === BATCH INSTANCES ===
	std::unordered_map<BatchKey, std::vector<uint32_t>, BatchKeyHash> batches;

	for (uint32_t i = 0; i < frameCtx.visibleInstances.size(); ++i) {
		GPUInstance inst = frameCtx.visibleInstances[i];

		if (inst.meshID < meshLods.size()) {
			const MeshLODs& lods = meshLods[inst.meshID];

			const glm::vec3 aabbOrigin = worldAABBs[i].origin;
			const float sphereRadius = worldAABBs[i].sphereRadius;

			float dist = glm::length(aabbOrigin - camPos) - sphereRadius;
			dist = std::max(0.0f, dist);

			const float projScaleY = cameraProj[1][1];
			const float screenRadius = (sphereRadius * projScaleY) / dist;

			uint32_t selectedMeshID = lods.lod0;

			if (screenRadius < 0.02f) {
				selectedMeshID = lods.lod3;
			}
			else if (screenRadius < 0.05f) {
				selectedMeshID = lods.lod2;
			}
			else if (screenRadius < 0.10f) {
				selectedMeshID = lods.lod1;
			}

			inst.meshID = selectedMeshID;
		}

		uint32_t index = static_cast<uint32_t>(visibleInstances.size());
		visibleInstances.push_back(inst);

		const BatchKey key{ inst.meshID, inst.materialID };
		batches[key].push_back(index);
	}

	frameCtx.indirectDraws.reserve(batches.size());

	// Rebuild instances
	frameCtx.visibleInstances.clear();
	frameCtx.visibleInstances.reserve(visibleInstances.size() + frameCtx.shadowCasterInstances.size());

	// === SPLIT BATCH KEYS INTO OPAQUE / TRANSPARENT ===
	std::vector<BatchKey> opaqueKeys;
	std::vector<BatchKey> transparentKeys;

	opaqueKeys.reserve(batches.size());
	transparentKeys.reserve(batches.size());

	for (const auto& [key, indices] : batches) {
		if (indices.empty()) continue;

		const GPUInstance& inst = visibleInstances[indices[0]];
		if (static_cast<MaterialPass>(inst.passType) == MaterialPass::Opaque) {
			opaqueKeys.push_back(key);
		}
		else {
			transparentKeys.push_back(key);
		}
	}

	// === OPAQUE BATCHES ===
	frameCtx.opaqueRange.firstCommand = 0;
	frameCtx.opaqueRange.firstInstance = static_cast<uint32_t>(frameCtx.visibleInstances.size());
	frameCtx.opaqueRange.commandCount = 0;
	frameCtx.opaqueRange.visibleCount = 0;

	for (const BatchKey& batchKey : opaqueKeys) {
		const std::vector<uint32_t>& instanceIndices = batches[batchKey];
		const GPUMeshData& mesh = meshes[batchKey.meshID];

		ASSERT(mesh.firstIndex + mesh.indexCount <= dbg.indexCount &&
			"[DrawPrep] Opaque draws would read past end of index buffer.");
		ASSERT(mesh.vertexOffset + mesh.vertexCount <= dbg.vertexCount &&
			"[DrawPrep] Opaque draws would read past end of vertex buffer.");

		const uint32_t firstInstance = static_cast<uint32_t>(frameCtx.visibleInstances.size());

		VkDrawIndexedIndirectCommand cmd{};
		cmd.indexCount = mesh.indexCount;
		cmd.instanceCount = static_cast<uint32_t>(instanceIndices.size());
		cmd.firstIndex = mesh.firstIndex;
		cmd.vertexOffset = static_cast<int32_t>(mesh.vertexOffset);
		cmd.firstInstance = firstInstance;

		frameCtx.indirectDraws.push_back(cmd);

		for (uint32_t idx : instanceIndices) {
			frameCtx.visibleInstances.push_back(visibleInstances[idx]);
		}

		frameCtx.opaqueRange.commandCount++;
		frameCtx.opaqueRange.visibleCount += cmd.instanceCount;
	}

	// === TRANSPARENT BATCHES ===
	frameCtx.transparentRange.firstCommand = frameCtx.opaqueRange.commandCount;
	frameCtx.transparentRange.firstInstance = static_cast<uint32_t>(frameCtx.visibleInstances.size());
	frameCtx.transparentRange.commandCount = 0;
	frameCtx.transparentRange.visibleCount = 0;

	for (const BatchKey& batchKey : transparentKeys) {
		const std::vector<uint32_t>& instanceIndices = batches[batchKey];
		const GPUMeshData& mesh = meshes[batchKey.meshID];

		ASSERT(mesh.firstIndex + mesh.indexCount <= dbg.indexCount &&
			"[DrawPrep] Transparent draws would read past end of index buffer.");
		ASSERT(mesh.vertexOffset + mesh.vertexCount <= dbg.vertexCount &&
			"[DrawPrep] Transparent draws would read past end of vertex buffer.");

		const uint32_t firstInstance = static_cast<uint32_t>(frameCtx.visibleInstances.size());

		VkDrawIndexedIndirectCommand cmd{};
		cmd.indexCount = mesh.indexCount;
		cmd.instanceCount = static_cast<uint32_t>(instanceIndices.size());
		cmd.firstIndex = mesh.firstIndex;
		cmd.vertexOffset = static_cast<int32_t>(mesh.vertexOffset);
		cmd.firstInstance = firstInstance;

		frameCtx.indirectDraws.push_back(cmd);

		for (uint32_t idx : instanceIndices) {
			frameCtx.visibleInstances.push_back(visibleInstances[idx]);
		}

		frameCtx.transparentRange.commandCount++;
		frameCtx.transparentRange.visibleCount += cmd.instanceCount;
	}

	// === SHADOW CASTERS (per cascade) ===
	if (dbg.enableShadows) {
		frameCtx.indirectDraws.reserve(frameCtx.indirectDraws.size() + frameCtx.shadowCasterInstances.size());

		auto& materialFlags = Engine::getState().getGPUResources().getMaterialFlagsByID();

		for (uint32_t cascadeIndex = 0; cascadeIndex < MAX_SHADOW_CASCADES; ++cascadeIndex) {
			const PassRange& inputRange = frameCtx.shadowCastersRanges[cascadeIndex];
			if (inputRange.visibleCount == 0u) continue;

			const uint32_t inputStart = inputRange.firstInstance;
			const uint32_t inputEnd = inputStart + inputRange.visibleCount;

			std::unordered_map<uint32_t, std::vector<uint32_t>> meshToInputIndices;
			meshToInputIndices.reserve(inputRange.visibleCount);

			for (uint32_t inputIndex = inputStart; inputIndex < inputEnd; ++inputIndex) {
				const GPUInstance& caster = frameCtx.shadowCasterInstances[inputIndex];

				uint32_t shadowMeshID = caster.meshID;
				if (shadowMeshID < meshLods.size()) {
					const MeshLODs& lods = meshLods[shadowMeshID];
					const uint32_t matFlag = materialFlags[caster.materialID];
					uint32_t lodSlot = getShadowMeshIDForCascade(lods, cascadeIndex);

					if ((matFlag & MATERIAL_FLAG_ALPHA_MASKED) != 0u) {
						lodSlot = applyFoliageBias(lodSlot, cascadeIndex);
					}

					shadowMeshID = shadowSlotToMeshID(lods, lodSlot);
				}

				meshToInputIndices[shadowMeshID].push_back(inputIndex);
			}

			std::vector<uint32_t> shadowMeshKeys;
			shadowMeshKeys.reserve(meshToInputIndices.size());
			for (const auto& it : meshToInputIndices) {
				shadowMeshKeys.push_back(it.first);
			}

			std::sort(shadowMeshKeys.begin(), shadowMeshKeys.end(),
				[](uint32_t a, uint32_t b) {
					return a < b;
				});

			PassRange& outRange = frameCtx.shadowDrawRanges[cascadeIndex];
			outRange.firstCommand = static_cast<uint32_t>(frameCtx.indirectDraws.size());
			outRange.firstInstance = static_cast<uint32_t>(frameCtx.visibleInstances.size());
			outRange.commandCount = 0u;
			outRange.visibleCount = 0u;

			for (uint32_t shadowMeshID : shadowMeshKeys) {
				const std::vector<uint32_t>& inputIndices = meshToInputIndices[shadowMeshID];
				const GPUMeshData& mesh = meshes[shadowMeshID];

				const uint32_t firstInstance = static_cast<uint32_t>(frameCtx.visibleInstances.size());

				VkDrawIndexedIndirectCommand cmd{};
				cmd.indexCount = mesh.shadowIndexCount;
				cmd.instanceCount = static_cast<uint32_t>(inputIndices.size());
				cmd.firstIndex = mesh.shadowFirstIndex;
				cmd.vertexOffset = static_cast<int32_t>(mesh.vertexOffset);
				cmd.firstInstance = firstInstance;

				frameCtx.indirectDraws.push_back(cmd);
				outRange.commandCount++;

				for (uint32_t inputIndex : inputIndices) {
					GPUInstance inst = frameCtx.shadowCasterInstances[inputIndex];
					inst.meshID = shadowMeshID;
					frameCtx.visibleInstances.push_back(inst);
				}

				outRange.visibleCount += cmd.instanceCount;
			}
		}
	}

	// === FLASHLIGHT SHADOW CASTERS ===
	if (LightingSystem::_mainFlashLight.isFlashLightOn()) {
		const PassRange& inputRange = frameCtx.flashLightShadowRange;
		if (inputRange.visibleCount > 0u) {

			const uint32_t inputStart = inputRange.firstInstance;
			const uint32_t inputEnd = inputStart + inputRange.visibleCount;

			std::unordered_map<uint32_t, std::vector<uint32_t>> meshToInputIndices;
			meshToInputIndices.reserve(inputRange.visibleCount);

			for (uint32_t inputIndex = inputStart; inputIndex < inputEnd; ++inputIndex) {
				const GPUInstance& caster = frameCtx.shadowCasterInstances[inputIndex];

				uint32_t shadowMeshID = caster.meshID;

				if (shadowMeshID < meshLods.size()) {
					const MeshLODs& lods = meshLods[shadowMeshID];
					shadowMeshID = lods.lod0;
				}

				meshToInputIndices[shadowMeshID].push_back(inputIndex);
			}

			std::vector<uint32_t> shadowMeshKeys;
			shadowMeshKeys.reserve(meshToInputIndices.size());
			for (const auto& it : meshToInputIndices) {
				shadowMeshKeys.push_back(it.first);
			}

			std::sort(shadowMeshKeys.begin(), shadowMeshKeys.end(),
				[](uint32_t a, uint32_t b) {
					return a < b;
				}
			);

			PassRange& outRange = frameCtx.flashLightShadowRange;
			outRange.firstCommand = static_cast<uint32_t>(frameCtx.indirectDraws.size());
			outRange.firstInstance = static_cast<uint32_t>(frameCtx.visibleInstances.size());
			outRange.commandCount = 0u;
			outRange.visibleCount = 0u;

			for (uint32_t shadowMeshID : shadowMeshKeys) {
				const std::vector<uint32_t>& inputIndices = meshToInputIndices[shadowMeshID];
				const GPUMeshData& mesh = meshes[shadowMeshID];

				const uint32_t firstInstance =
					static_cast<uint32_t>(frameCtx.visibleInstances.size());

				VkDrawIndexedIndirectCommand cmd{};
				cmd.indexCount = mesh.shadowIndexCount;
				cmd.instanceCount = static_cast<uint32_t>(inputIndices.size());
				cmd.firstIndex = mesh.shadowFirstIndex;
				cmd.vertexOffset = static_cast<int32_t>(mesh.vertexOffset);
				cmd.firstInstance = firstInstance;

				frameCtx.indirectDraws.push_back(cmd);
				outRange.commandCount++;

				for (uint32_t inputIndex : inputIndices) {
					GPUInstance inst = frameCtx.shadowCasterInstances[inputIndex];
					inst.meshID = shadowMeshID;
					frameCtx.visibleInstances.push_back(inst);
				}

				outRange.visibleCount += cmd.instanceCount;
			}

			ASSERT(outRange.commandCount > 0u);
			ASSERT(outRange.visibleCount > 0u);

			ASSERT(outRange.firstCommand + outRange.commandCount <= frameCtx.indirectDraws.size());
			ASSERT(outRange.firstInstance + outRange.visibleCount <= frameCtx.visibleInstances.size());
		}
	}
}


inline static CombinedUploadPlan stageCombinedUploads(FrameContext& frame,
	VmaAllocator alloc,
	bool isGPUAccelOn)
{
	CombinedUploadPlan plan{};
	auto* mapped = static_cast<uint8_t*>(frame.combinedGPUStaging.info.pMappedData);
	const size_t cap = frame.combinedGPUStaging.info.size;

	if (!isGPUAccelOn) {
		// visible instances
		plan.visSize = frame.visibleInstances.size() * sizeof(GPUInstance);
		plan.visOff = BufferUtils::reserveStaging(frame.stagingHead, cap, plan.visSize);
		memcpy(mapped + plan.visOff, frame.visibleInstances.data(), plan.visSize);
		BufferUtils::flushStagingRange(frame.combinedGPUStaging.allocation, plan.visOff, plan.visSize, alloc);

		// indirect
		plan.indSize = frame.indirectDraws.size() * sizeof(VkDrawIndexedIndirectCommand);
		plan.indOff = BufferUtils::reserveStaging(frame.stagingHead, cap, plan.indSize);
		memcpy(mapped + plan.indOff, frame.indirectDraws.data(), plan.indSize);
		BufferUtils::flushStagingRange(frame.combinedGPUStaging.allocation, plan.indOff, plan.indSize, alloc);
	}

	// frame address table
	if (frame.addressTable.isTableDirty()) {
		plan.fAddrSize = sizeof(GPUAddressTable);
		plan.fAddrOff = BufferUtils::reserveStaging(frame.stagingHead, cap, plan.fAddrSize);
		memcpy(mapped + plan.fAddrOff, &frame.addressTable, plan.fAddrSize);
		BufferUtils::flushStagingRange(frame.combinedGPUStaging.allocation, plan.fAddrOff, plan.fAddrSize, alloc);
	}

	return plan;
}

void DrawPreparation::uploadGPUBuffersForFrame(
	FrameContext& frameCtx,
	GPUResources& gpuResources,
	const std::vector<glm::mat4>& transforms,
	const std::vector<LocalLight>& lights,
	GPUQueue& transferQueue,
	bool isTemporalValid,
	bool isGPUAccelOn)
{
	ASSERT(frameCtx.combinedGPUStaging.buffer != VK_NULL_HANDLE &&
		"[DrawPreparation::uploadGPUBuffersForFrame] combinedGPUstaging buffer is invalid.");

	const auto alloc = gpuResources.getAllocator();

	CombinedUploadPlan plan = stageCombinedUploads(frameCtx, alloc, isGPUAccelOn);

	// Global light staging prep
	const auto& lightStaging = gpuResources.getLightListStagingBuffer();
	size_t loadedLightsSize = 0;
	if (!lights.empty() && frameCtx.lightsBufferUploadNeeded) {
		ASSERT(lightStaging.buffer != VK_NULL_HANDLE);

		auto* mapped = static_cast<uint8_t*>(lightStaging.info.pMappedData);
		loadedLightsSize = lights.size() * sizeof(LocalLight);
		memcpy(mapped, lights.data(), loadedLightsSize);
		BufferUtils::flushStagingRange(
			lightStaging.allocation,
			0,
			loadedLightsSize,
			alloc
		);
	}
	const size_t totalLightBufferBytes = loadedLightsSize;


	// Global transforms staging prep
	auto& transformStaging = gpuResources.getInstanceTransformsStagingBuffer();
	size_t activeTransformsSize = 0;
	if (frameCtx.transformsBufferUploadNeeded) {
		ASSERT(transformStaging.buffer != VK_NULL_HANDLE);

		auto* mapped = static_cast<uint8_t*>(transformStaging.info.pMappedData);
		activeTransformsSize = transforms.size() * sizeof(glm::mat4);
		memcpy(mapped, transforms.data(), activeTransformsSize);
		BufferUtils::flushStagingRange(
			transformStaging.allocation,
			0,
			activeTransformsSize,
			alloc
		);
	}
	const size_t totalTransformsBufferBytes = activeTransformsSize;

	// Record big transfer copies for dynamic frame data
	CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {
		if (frameCtx.addressTable.isTableDirty()) {
			// frame GPU address table copy
			VkBufferCopy frameAddressTableCpy{};
			frameAddressTableCpy.srcOffset = plan.fAddrOff;
			frameAddressTableCpy.dstOffset = 0;
			frameAddressTableCpy.size = plan.fAddrSize;
			vkCmdCopyBuffer(cmd,
				frameCtx.combinedGPUStaging.buffer,
				frameCtx.addressTable_GPU.buffer,
				1,
				&frameAddressTableCpy);

			if (isGPUAccelOn) {
				BarrierUtils::bufferTransferReleaseOnCompute(cmd, frameCtx.addressTable_GPU);
			}
			else {
				BarrierUtils::bufferTransferReleaseOnGraphics(cmd, frameCtx.addressTable_GPU);
			}
		}

		if (!isGPUAccelOn) {
			// visible instance data
			VkBufferCopy visInstCpy{};
			visInstCpy.srcOffset = plan.visOff;
			visInstCpy.dstOffset = 0;
			visInstCpy.size = plan.visSize;
			vkCmdCopyBuffer(cmd,
				frameCtx.combinedGPUStaging.buffer,
				frameCtx.visibleInstances_GPU.buffer,
				1,
				&visInstCpy);

			// indirect draw commands
			VkBufferCopy indirectDrawsCpy{};
			indirectDrawsCpy.srcOffset = plan.indOff;
			indirectDrawsCpy.dstOffset = 0;
			indirectDrawsCpy.size = plan.indSize;
			vkCmdCopyBuffer(cmd,
				frameCtx.combinedGPUStaging.buffer,
				frameCtx.indirectDraws_GPU.buffer,
				1,
				&indirectDrawsCpy);

			BarrierUtils::bufferTransferReleaseOnGraphics(cmd, frameCtx.visibleInstances_GPU);
			BarrierUtils::bufferTransferReleaseOnIndirect(cmd, frameCtx.indirectDraws_GPU);
		}

		if (frameCtx.transformsBufferUploadNeeded) {
			if (isTemporalValid) {
				const auto& lastFrame = Renderer::getLastFrame();
				// Copy GPU current (last frames data) transforms into previous
				VkBufferCopy copyTransformsCpy{};
				copyTransformsCpy.srcOffset = 0;
				copyTransformsCpy.dstOffset = 0;
				copyTransformsCpy.size = totalTransformsBufferBytes;
				vkCmdCopyBuffer(cmd,
					lastFrame.transforms_GPU.buffer,
					frameCtx.prevTransforms_GPU.buffer,
					1,
					&copyTransformsCpy);
			}

			// Upload new transforms to current
			VkBufferCopy uploadNewTransformsCpy{};
			uploadNewTransformsCpy.srcOffset = 0;
			uploadNewTransformsCpy.dstOffset = 0;
			uploadNewTransformsCpy.size = totalTransformsBufferBytes;
			vkCmdCopyBuffer(cmd,
				transformStaging.buffer,
				frameCtx.transforms_GPU.buffer,
				1,
				&uploadNewTransformsCpy);

			BarrierUtils::bufferTransferReleaseOnGraphics(cmd, frameCtx.transforms_GPU);

			if (isTemporalValid) {
				BarrierUtils::bufferTransferReleaseOnGraphics(cmd, frameCtx.prevTransforms_GPU);
			}
		}

		if (!lights.empty() && frameCtx.lightsBufferUploadNeeded) {
			VkBufferCopy lightCpy{};
			lightCpy.srcOffset = 0;
			lightCpy.dstOffset = 0;
			lightCpy.size = totalLightBufferBytes;
			vkCmdCopyBuffer(cmd,
				lightStaging.buffer,
				frameCtx.lights_GPU.buffer,
				1,
				&lightCpy);

			BarrierUtils::bufferTransferReleaseOnGraphics(cmd, frameCtx.lights_GPU);
		}

	}, frameCtx.transferPool, QueueType::Transfer, Backend::getDevice());

	frameCtx.collectAndAppendCmds(std::move(DeferredCmdSubmitQueue::collectTransfer()), QueueType::Transfer);

	auto& transferSync = Renderer::_transferSync;
	const uint64_t signalValue = transferQueue.submitWithTimelineSync(
		frameCtx.transferCmds,
		transferSync.semaphore,
		++transferSync.signalValue
	);

	frameCtx.stashSubmitted(QueueType::Transfer);
	frameCtx.transferWaitValue = signalValue;
}

static glm::mat4 makeGridTransform3D(uint32_t index, uint32_t count, float spacing)
{
	ASSERT(count > 0);

	const float cubeRoot = std::cbrt(static_cast<float>(count));
	const uint32_t gridDim = glm::max(1u, static_cast<uint32_t>(std::ceil(cubeRoot)));

	const uint32_t layerSize = gridDim * gridDim;

	const uint32_t y = index / layerSize;
	const uint32_t rem = index - y * layerSize;

	const uint32_t z = rem / gridDim;
	const uint32_t x = rem - z * gridDim;

	const glm::vec3 translation(
		static_cast<float>(x) * spacing,
		static_cast<float>(y) * spacing,
		static_cast<float>(z) * spacing);

	return glm::translate(glm::mat4(1.0f), translation);
}

static bool braindeadhack = false;

bool DrawPreparation::syncGlobalInstancesAndTransforms(
	std::unordered_map<SceneID, SceneProfileEntry>& sceneProfiles,
	std::vector<GlobalInstance>& globalInstances,
	std::vector<glm::mat4>& globalTransforms,
	const double deltaTime)
{
	bool transformsUpdated = false;

	for (auto& inst : globalInstances) {
		SceneID sid = static_cast<SceneID>(inst.sceneID);
		SceneProfileEntry& profile = sceneProfiles.at(sid);

		if (profile.instanceCount == 1) {
			if (profile.drawType == DrawType::DrawStatic) {
				// TODO: Create a way to modify transforms at runtime
				if (!braindeadhack && profile.name == "DamagedHelmet") {
					glm::mat4& M = globalTransforms[inst.firstTransform];
					// Turn helmet for bake
					M = glm::rotate(M, glm::radians(-90.0f), glm::vec3(0.0f, 0.0f, 1.0f));

					inst.baseTransform = M;
					transformsUpdated = true;
					braindeadhack = true;
				}
				inst.drawType = profile.drawType;
				continue; // transforms already baked into static
			}
			if (profile.drawType == DrawType::DrawDynamic) {
				inst.drawType = profile.drawType;

				constexpr float spinSpeedRadiansPerSecond = glm::radians(30.0f);
				const float deltaSecondsFloat = static_cast<float>(deltaTime);

				inst.spinAngleRadians += spinSpeedRadiansPerSecond * deltaSecondsFloat;

				const glm::mat4& baseTransform = inst.baseTransform;
				const glm::vec3 pivot = glm::vec3(baseTransform[3]);
				inst.modelOffset = glm::vec3(0.0f, 2.5f, 0.0f);

				const glm::mat4 newTransform =
					glm::translate(glm::mat4(1.0f), pivot) *
					glm::rotate(glm::mat4(1.0f), inst.spinAngleRadians, glm::vec3(0.0f, 1.0f, 0.0f)) *
					glm::translate(glm::mat4(1.0f), -pivot) *
					glm::translate(glm::mat4(1.0f), inst.modelOffset) *
					baseTransform;

				globalTransforms[inst.firstTransform] = newTransform;

				transformsUpdated = true;
				continue;
			}
		//	if (profile.drawType == DrawType::DrawDynamic) {
		//		inst.drawType = profile.drawType;

		//		const float deltaSecondsFloat = static_cast<float>(deltaTime);

		//		// Advance a phase accumulator (store this per instance like you do spinAngleRadians)
		//		const float moveSpeedRadiansPerSecond = 1.5f; // tweak
		//		inst.movePhaseRadians += moveSpeedRadiansPerSecond * deltaSecondsFloat;

		//		// Line direction and amplitude
		//		const glm::vec3 lineDir = glm::normalize(glm::vec3(1.0f, 0.0f, 0.0f)); // X axis
		//		const float amplitude = 2.0f; // world units

		//		const float t = std::sin(inst.movePhaseRadians);
		//		const glm::vec3 moveOffset = lineDir * (t * amplitude);

		//		const glm::mat4& baseTransform = inst.baseTransform;

		//		glm::mat4 newTransform = baseTransform;
		//		newTransform[3] = baseTransform[3] + glm::vec4(moveOffset, 0.0f);

		//		globalTransforms[inst.firstTransform] = newTransform;

		//		transformsUpdated = true;
		//		continue;
		//	}
		}

		// TODO: ADD SUPPORT FOR MULTI-DYNAMIC

		// Defined from copy values append list or decrease list
		// on first run this will always be an append
		if (profile.drawType == DrawType::DrawMultiStatic || profile.instanceCount > 1) {
			inst.drawType = profile.drawType;

			const uint32_t desiredUsedCopies = std::max(1u, profile.instanceCount);

			const uint32_t desiredCapacityCopies = desiredUsedCopies;

			if (inst.usedCopies == desiredUsedCopies && inst.capacityCopies >= desiredCapacityCopies) {
				continue;
			}

			const uint32_t transformsPerCopy = inst.transformCount;
			ASSERT(transformsPerCopy > 0);

			const uint32_t oldCapacityCopies = inst.capacityCopies;
			const uint32_t oldSlabTransformCount = oldCapacityCopies * transformsPerCopy;
			const uint32_t oldSlabBegin = inst.firstTransform;
			const uint32_t oldSlabEnd = oldSlabBegin + oldSlabTransformCount;

			// We can only append in-place if this slab currently ends at the end of the global array.
			const bool slabIsAtEnd = (oldSlabEnd == globalTransforms.size());

			if (desiredCapacityCopies > oldCapacityCopies) {
				const glm::mat4 baseTransform = globalTransforms[oldSlabBegin];

				//fmt::print("[syncGI] multistatic: oldCap={} newCap={} firstT={} tfCount={} slabEnd={} tfSize(before)={}\n",
				//	oldCapacityCopies,
				//	desiredCapacityCopies,
				//	inst.firstTransform,
				//	transformsPerCopy,
				//	oldSlabEnd,
				//	globalTransforms.size());

				if (!slabIsAtEnd) {
					// Relocate this slab to the end to preserve the "contiguous slab" invariant.
					const uint32_t newFirstTransform = static_cast<uint32_t>(globalTransforms.size());

					// Copy old slab transforms.
					for (uint32_t i = 0; i < oldSlabTransformCount; ++i) {
						globalTransforms.push_back(globalTransforms[static_cast<size_t>(oldSlabBegin + i)]);
					}

					inst.firstTransform = newFirstTransform;
				}

				// Append transforms for new copies (copy indices [oldCap .. newCap)).
				for (uint32_t copyIndex = oldCapacityCopies; copyIndex < desiredCapacityCopies; ++copyIndex) {
					glm::mat4 offset = makeGridTransform3D(copyIndex, desiredCapacityCopies, 5.0f);

					for (uint32_t slot = 0; slot < transformsPerCopy; ++slot) {
						const uint32_t baseSlotIndex = inst.firstTransform + slot; // copy 0, slot N
						const glm::mat4 slotBaseTransform = globalTransforms[baseSlotIndex];

						globalTransforms.push_back(offset * slotBaseTransform);
					}
				}

				//fmt::print("[syncGI] tfSize(after)={} firstT={} relocated={}\n",
				//	globalTransforms.size(),
				//	inst.firstTransform,
				//	(!slabIsAtEnd));

				inst.capacityCopies = desiredCapacityCopies;
				transformsUpdated = true;
			}

			// Change active copies (visibility will rebuild/activate).
			inst.usedCopies = desiredUsedCopies;
		}
	}

	return transformsUpdated;
}
