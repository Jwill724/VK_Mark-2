#include "pch.h"

#include "DrawPreparation.h"
#include "engine/Engine.h"
#include "utils/BufferUtils.h"

struct BatchKey
{
	uint32_t meshID;
	uint32_t materialID;

	bool operator==(const BatchKey& other) const {
		return meshID == other.meshID && materialID == other.materialID;
	}
};

struct BatchKeyHash
{
	std::size_t operator()(const BatchKey& k) const {
		std::size_t h1 = std::hash<uint32_t>{}(k.meshID);
		std::size_t h2 = std::hash<uint32_t>{}(k.materialID);
		return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
	}
};

struct ShadowBatchKey
{
	uint32_t meshID;

	bool operator==(const ShadowBatchKey& other) const {
		return meshID == other.meshID;
	}
};

struct ShadowBatchKeyHash
{
	std::size_t operator()(const ShadowBatchKey& k) const {
		return std::hash<uint32_t>{}(k.meshID);
	}
};

struct CombinedUploadPlan
{
	// frame-local
	size_t visOff = 0, visSize = 0; // visible instances
	size_t indOff = 0, indSize = 0; // indirect draws

	// Only required when visibles and draw buffers are created or destroyed.
	// per-frame address table (frameCtx.m_addressTable_GPU)
	size_t fAddrOff = 0, fAddrSize = 0;
	uint32_t addrVersion = UINT32_MAX;
};

struct TransparentEntry {
	uint32_t instanceIndex;
	float distSq;
};



// All render data for FrameContext is reset prior to this function.
// When inputs are entered, visibleInstances and worldAABBS are be 1:1 in access before batched/sorted.
//
// Goal is to create a mega indirect draw buffer with takes many paths.
// Many instance paths are stored in their own containers but the main visible m_frameSet
// visibleInstances is filled with opaque and transparent and here all other paths are built
// to append to the visible instances buffer.
void DrawPreparation::buildAndSortIndirectDraws(
	FrameContext& frameCtx,
	const std::vector<Mesh>& meshes,
	const std::vector<MeshLODs>& meshLods,
	const std::vector<AABB>& worldAABBs,
	const glm::vec4& cameraPos,
	const glm::mat4& cameraProj,
	const RenderToggles& dbg)
{
	std::vector<Instance> visibleInstances;

	visibleInstances.reserve(frameCtx.m_visibleInstances.size());

	ASSERT(frameCtx.m_visibleInstances.size() == worldAABBs.size() && "Visible Instances should be 1:1 with world AABBs.");

	const glm::vec3 camPos = glm::vec3(cameraPos);

	// === BATCH INSTANCES ===
	std::unordered_map<BatchKey, std::vector<uint32_t>, BatchKeyHash> batches;

	for (uint32_t i = 0; i < frameCtx.m_visibleInstances.size(); ++i) {
		Instance inst = frameCtx.m_visibleInstances[i];

		if (inst.meshID < meshLods.size()) {
			const MeshLODs& lods = meshLods[inst.meshID];

			const AABB& worldAABB = worldAABBs[i];

			const glm::vec3 aabbOrigin = 0.5f * (worldAABB.vmin + worldAABB.vmax);
			const glm::vec3 extent = 0.5f * (worldAABB.vmax - worldAABB.vmin);
			const float sphereRadius = glm::length(extent);

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

	frameCtx.m_indirectDraws.reserve(batches.size());

	// Rebuild instances
	frameCtx.m_visibleInstances.clear();
	frameCtx.m_visibleInstances.reserve(visibleInstances.size() + frameCtx.m_visibleShadowCasters.size());

	// === SPLIT BATCH KEYS INTO OPAQUE / TRANSPARENT ===
	std::vector<BatchKey> opaqueKeys;
	std::vector<BatchKey> transparentKeys;

	opaqueKeys.reserve(batches.size());
	transparentKeys.reserve(batches.size());

	for (const auto& [key, indices] : batches) {
		if (indices.empty()) continue;

		const Instance& inst = visibleInstances[indices[0]];
		if (static_cast<MaterialPass>(inst.passType) == MaterialPass::OPAQUE) {
			opaqueKeys.push_back(key);
		}
		else {
			transparentKeys.push_back(key);
		}
	}

	// === OPAQUE BATCHES ===
	frameCtx.m_opaqueDrawRange.firstCommand = 0;
	frameCtx.m_opaqueDrawRange.firstInstance = static_cast<uint32_t>(frameCtx.m_visibleInstances.size());
	frameCtx.m_opaqueDrawRange.commandCount = 0;
	frameCtx.m_opaqueDrawRange.visibleCount = 0;

	for (const BatchKey& batchKey : opaqueKeys) {
		const std::vector<uint32_t>& instanceIndices = batches[batchKey];
		const Mesh& mesh = meshes[batchKey.meshID];

		ASSERT(mesh.firstIndex + mesh.indexCount <= dbg.indexCount &&
			"[DrawPrep] Opaque draws would read past end of index buffer.");
		ASSERT(mesh.vertexOffset + mesh.vertexCount <= dbg.vertexCount &&
			"[DrawPrep] Opaque draws would read past end of vertex buffer.");

		const uint32_t firstInstance = static_cast<uint32_t>(frameCtx.m_visibleInstances.size());

		VkDrawIndexedIndirectCommand cmd{};
		cmd.indexCount = mesh.indexCount;
		cmd.instanceCount = static_cast<uint32_t>(instanceIndices.size());
		cmd.firstIndex = mesh.firstIndex;
		cmd.vertexOffset = static_cast<int32_t>(mesh.vertexOffset);
		cmd.firstInstance = firstInstance;

		frameCtx.m_indirectDraws.push_back(cmd);

		for (uint32_t idx : instanceIndices) {
			frameCtx.m_visibleInstances.push_back(visibleInstances[idx]);
		}

		frameCtx.m_opaqueDrawRange.commandCount++;
		frameCtx.m_opaqueDrawRange.visibleCount += cmd.instanceCount;
	}

	// === TRANSPARENT BATCHES ===
	frameCtx.m_transparentDrawRange.firstCommand = frameCtx.m_opaqueDrawRange.commandCount;
	frameCtx.m_transparentDrawRange.firstInstance = static_cast<uint32_t>(frameCtx.m_visibleInstances.size());
	frameCtx.m_transparentDrawRange.commandCount = 0;
	frameCtx.m_transparentDrawRange.visibleCount = 0;

	for (const BatchKey& batchKey : transparentKeys) {
		const std::vector<uint32_t>& instanceIndices = batches[batchKey];
		const Mesh& mesh = meshes[batchKey.meshID];

		ASSERT(mesh.firstIndex + mesh.indexCount <= dbg.indexCount &&
			"[DrawPrep] Transparent draws would read past end of index buffer.");
		ASSERT(mesh.vertexOffset + mesh.vertexCount <= dbg.vertexCount &&
			"[DrawPrep] Transparent draws would read past end of vertex buffer.");

		const uint32_t firstInstance = static_cast<uint32_t>(frameCtx.m_visibleInstances.size());

		VkDrawIndexedIndirectCommand cmd{};
		cmd.indexCount = mesh.indexCount;
		cmd.instanceCount = static_cast<uint32_t>(instanceIndices.size());
		cmd.firstIndex = mesh.firstIndex;
		cmd.vertexOffset = static_cast<int32_t>(mesh.vertexOffset);
		cmd.firstInstance = firstInstance;

		frameCtx.m_indirectDraws.push_back(cmd);

		for (uint32_t idx : instanceIndices) {
			frameCtx.m_visibleInstances.push_back(visibleInstances[idx]);
		}

		frameCtx.m_transparentDrawRange.commandCount++;
		frameCtx.m_transparentDrawRange.visibleCount += cmd.instanceCount;
	}

	// === SHADOW CASTERS (per cascade) ===
	if (dbg.enableShadows) {
		frameCtx.m_indirectDraws.reserve(frameCtx.m_indirectDraws.size() + frameCtx.m_visibleShadowCasters.size());

		auto& materialFlags = Engine::GetState().getGPUResources().GetMaterialFlagsByID();

		for (uint32_t cascadeIndex = 0; cascadeIndex < MAX_SHADOW_CASCADES; ++cascadeIndex) {
			const IndirectDrawRange& inputRange = frameCtx.m_shadowCasterDrawRanges[cascadeIndex];
			if (inputRange.visibleCount == 0u) continue;

			const uint32_t inputStart = inputRange.firstInstance;
			const uint32_t inputEnd = inputStart + inputRange.visibleCount;

			std::unordered_map<uint32_t, std::vector<uint32_t>> meshToInputIndices;
			meshToInputIndices.reserve(inputRange.visibleCount);

			for (uint32_t inputIndex = inputStart; inputIndex < inputEnd; ++inputIndex) {
				const Instance& caster = frameCtx.m_visibleShadowCasters[inputIndex];

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

			IndirectDrawRange& outRange = frameCtx.m_shadowDrawRanges[cascadeIndex];
			outRange.firstCommand = static_cast<uint32_t>(frameCtx.m_indirectDraws.size());
			outRange.firstInstance = static_cast<uint32_t>(frameCtx.m_visibleInstances.size());
			outRange.commandCount = 0u;
			outRange.visibleCount = 0u;

			for (uint32_t shadowMeshID : shadowMeshKeys) {
				const std::vector<uint32_t>& inputIndices = meshToInputIndices[shadowMeshID];
				const Mesh& mesh = meshes[shadowMeshID];

				const uint32_t firstInstance = static_cast<uint32_t>(frameCtx.m_visibleInstances.size());

				VkDrawIndexedIndirectCommand cmd{};
				cmd.indexCount = mesh.shadowIndexCount;
				cmd.instanceCount = static_cast<uint32_t>(inputIndices.size());
				cmd.firstIndex = mesh.shadowFirstIndex;
				cmd.vertexOffset = static_cast<int32_t>(mesh.vertexOffset);
				cmd.firstInstance = firstInstance;

				frameCtx.m_indirectDraws.push_back(cmd);
				outRange.commandCount++;

				for (uint32_t inputIndex : inputIndices) {
					Instance inst = frameCtx.m_visibleShadowCasters[inputIndex];
					inst.meshID = shadowMeshID;
					frameCtx.m_visibleInstances.push_back(inst);
				}

				outRange.visibleCount += cmd.instanceCount;
			}
		}
	}

	// === FLASHLIGHT SHADOW CASTERS ===
	if (LightingSystem::_mainFlashLight.IsFlashLightOn()) {
		const IndirectDrawRange& inputRange = frameCtx.m_flashlightShadowCasterRange;
		if (inputRange.visibleCount > 0u) {

			const uint32_t inputStart = inputRange.firstInstance;
			const uint32_t inputEnd = inputStart + inputRange.visibleCount;

			std::unordered_map<uint32_t, std::vector<uint32_t>> meshToInputIndices;
			meshToInputIndices.reserve(inputRange.visibleCount);

			for (uint32_t inputIndex = inputStart; inputIndex < inputEnd; ++inputIndex) {
				const Instance& caster = frameCtx.m_visibleShadowCasters[inputIndex];

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

			IndirectDrawRange& outRange = frameCtx.m_flashlightShadowCasterRange;
			outRange.firstCommand = static_cast<uint32_t>(frameCtx.m_indirectDraws.size());
			outRange.firstInstance = static_cast<uint32_t>(frameCtx.m_visibleInstances.size());
			outRange.commandCount = 0u;
			outRange.visibleCount = 0u;

			for (uint32_t shadowMeshID : shadowMeshKeys) {
				const std::vector<uint32_t>& inputIndices = meshToInputIndices[shadowMeshID];
				const Mesh& mesh = meshes[shadowMeshID];

				const uint32_t firstInstance =
					static_cast<uint32_t>(frameCtx.m_visibleInstances.size());

				VkDrawIndexedIndirectCommand cmd{};
				cmd.indexCount = mesh.shadowIndexCount;
				cmd.instanceCount = static_cast<uint32_t>(inputIndices.size());
				cmd.firstIndex = mesh.shadowFirstIndex;
				cmd.vertexOffset = static_cast<int32_t>(mesh.vertexOffset);
				cmd.firstInstance = firstInstance;

				frameCtx.m_indirectDraws.push_back(cmd);
				outRange.commandCount++;

				for (uint32_t inputIndex : inputIndices) {
					Instance inst = frameCtx.m_visibleShadowCasters[inputIndex];
					inst.meshID = shadowMeshID;
					frameCtx.m_visibleInstances.push_back(inst);
				}

				outRange.visibleCount += cmd.instanceCount;
			}

			ASSERT(outRange.commandCount > 0u);
			ASSERT(outRange.visibleCount > 0u);

			ASSERT(outRange.firstCommand + outRange.commandCount <= frameCtx.m_indirectDraws.size());
			ASSERT(outRange.firstInstance + outRange.visibleCount <= frameCtx.m_visibleInstances.size());
		}
	}
}


inline static CombinedUploadPlan stageCombinedUploads(FrameContext& frame,
	VmaAllocator alloc,
	bool isGPUAccelOn)
{
	CombinedUploadPlan plan{};
	auto* mapped = static_cast<uint8_t*>(frame.m_gpuCopyStaging.m_allocInfo.pMappedData);
	const size_t cap = frame.m_gpuCopyStaging.m_allocInfo.size;

	if (!isGPUAccelOn && !frame.m_visibleInstances.empty()) {
		// visible instances
		plan.visSize = frame.m_visibleInstances.size() * sizeof(Instance);
		plan.visOff = BufferUtils::ReserveStaging(frame.m_gpuCopyStagingHead, cap, plan.visSize);
		memcpy(mapped + plan.visOff, frame.m_visibleInstances.data(), plan.visSize);
		BufferUtils::FlushStagingRange(frame.m_gpuCopyStaging.m_allocation, plan.visOff, plan.visSize, alloc);

		// indirect
		plan.indSize = frame.m_indirectDraws.size() * sizeof(VkDrawIndexedIndirectCommand);
		plan.indOff = BufferUtils::ReserveStaging(frame.m_gpuCopyStagingHead, cap, plan.indSize);
		memcpy(mapped + plan.indOff, frame.m_indirectDraws.data(), plan.indSize);
		BufferUtils::FlushStagingRange(frame.m_gpuCopyStaging.m_allocation, plan.indOff, plan.indSize, alloc);
	}

	// frame address table
	if (frame.m_gpuAddressTable.IsVersionMismatched()) {
		plan.fAddrSize = sizeof(BindlessBufferTable);
		plan.fAddrOff = BufferUtils::ReserveStaging(frame.m_gpuCopyStagingHead, cap, plan.fAddrSize);
		memcpy(mapped + plan.fAddrOff, &frame.m_gpuAddressTable, plan.fAddrSize);
		BufferUtils::FlushStagingRange(frame.m_gpuCopyStaging.m_allocation, plan.fAddrOff, plan.fAddrSize, alloc);

		plan.addrVersion = frame.m_gpuAddressTable.GetCpuVersion();
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
	ASSERT(frameCtx.m_gpuCopyStaging.m_buffer != VK_NULL_HANDLE &&
		"[DrawPreparation::uploadGPUBuffersForFrame] combinedGPUstaging buffer is invalid.");

	const auto alloc = gpuResources.GetAllocator();

	CombinedUploadPlan plan = stageCombinedUploads(frameCtx, alloc, isGPUAccelOn);

	// Global light staging prep
	const auto& lightStaging = gpuResources.GetLightListStagingBuffer();
	size_t loadedLightsSize = 0;
	if (!lights.empty() && frameCtx.m_bLightsBufferUploadNeeded) {
		ASSERT(lightStaging.m_buffer != VK_NULL_HANDLE);

		auto* mapped = static_cast<uint8_t*>(lightStaging.m_allocInfo.pMappedData);
		loadedLightsSize = lights.size() * sizeof(LocalLight);
		memcpy(mapped, lights.data(), loadedLightsSize);
		BufferUtils::FlushStagingRange(
			lightStaging.m_allocation,
			0,
			loadedLightsSize,
			alloc
		);
	}
	const size_t totalLightBufferBytes = loadedLightsSize;


	// Global transforms staging prep
	auto& transformStaging = gpuResources.GetInstanceTransformsStagingBuffer();
	size_t activeTransformsSize = 0;
	if (frameCtx.m_bTransformsBufferUploadNeeded) {
		ASSERT(transformStaging.m_buffer != VK_NULL_HANDLE);

		auto* mapped = static_cast<uint8_t*>(transformStaging.m_allocInfo.pMappedData);
		activeTransformsSize = transforms.size() * sizeof(glm::mat4);
		memcpy(mapped, transforms.data(), activeTransformsSize);
		BufferUtils::FlushStagingRange(
			transformStaging.m_allocation,
			0,
			activeTransformsSize,
			alloc
		);
	}
	const size_t totalTransformsBufferBytes = activeTransformsSize;

	// Record big transfer copies for dynamic frame data
	CommandBuffer::RecordCommandBuffer([&](VkCommandBuffer cmd) {
		if (plan.fAddrSize > 0) {
			// frame GPU address table copy
			VkBufferCopy frameAddressTableCpy{};
			frameAddressTableCpy.srcOffset = plan.fAddrOff;
			frameAddressTableCpy.dstOffset = 0;
			frameAddressTableCpy.size = plan.fAddrSize;
			vkCmdCopyBuffer(cmd,
				frameCtx.m_gpuCopyStaging.m_buffer,
				frameCtx.m_addressTable_GPU.m_buffer,
				1,
				&frameAddressTableCpy);

			frameCtx.m_pendingAddressTableVersion = plan.addrVersion;

			if (isGPUAccelOn) {
				BarrierUtils::BufferTransferReleaseOnCompute(cmd, frameCtx.m_addressTable_GPU);
			}
			else {
				BarrierUtils::BufferTransferReleaseOnGraphics(cmd, frameCtx.m_addressTable_GPU);
			}
		}

		if (!isGPUAccelOn && !frameCtx.m_visibleInstances.empty()) {
			// visible instance data
			VkBufferCopy visInstCpy{};
			visInstCpy.srcOffset = plan.visOff;
			visInstCpy.dstOffset = 0;
			visInstCpy.size = plan.visSize;
			vkCmdCopyBuffer(cmd,
				frameCtx.m_gpuCopyStaging.m_buffer,
				frameCtx.m_visibleInstances_GPU.m_buffer,
				1,
				&visInstCpy);

			// indirect draw commands
			VkBufferCopy indirectDrawsCpy{};
			indirectDrawsCpy.srcOffset = plan.indOff;
			indirectDrawsCpy.dstOffset = 0;
			indirectDrawsCpy.size = plan.indSize;
			vkCmdCopyBuffer(cmd,
				frameCtx.m_gpuCopyStaging.m_buffer,
				frameCtx.m_indirectDraws_GPU.m_buffer,
				1,
				&indirectDrawsCpy);

			BarrierUtils::BufferTransferReleaseOnGraphics(cmd, frameCtx.m_visibleInstances_GPU);
			BarrierUtils::BufferTransferReleaseOnIndirect(cmd, frameCtx.m_indirectDraws_GPU);
		}

		if (!transforms.empty() && frameCtx.m_bTransformsBufferUploadNeeded) {
			if (isTemporalValid) {
				const auto& lastFrame = Renderer::GetLastFrame();
				// Copy GPU current (last frames data) transforms into previous
				VkBufferCopy copyTransformsCpy{};
				copyTransformsCpy.srcOffset = 0;
				copyTransformsCpy.dstOffset = 0;
				copyTransformsCpy.size = totalTransformsBufferBytes;
				vkCmdCopyBuffer(cmd,
					lastFrame.transforms_GPU.buffer,
					frameCtx.m_prevTransforms_GPU.m_buffer,
					1,
					&copyTransformsCpy);
			}

			// Upload new transforms to current
			VkBufferCopy uploadNewTransformsCpy{};
			uploadNewTransformsCpy.srcOffset = 0;
			uploadNewTransformsCpy.dstOffset = 0;
			uploadNewTransformsCpy.size = totalTransformsBufferBytes;
			vkCmdCopyBuffer(cmd,
				transformStaging.m_buffer,
				frameCtx.m_transforms_GPU.m_buffer,
				1,
				&uploadNewTransformsCpy);

			BarrierUtils::BufferTransferReleaseOnGraphics(cmd, frameCtx.m_transforms_GPU);

			if (isTemporalValid) {
				BarrierUtils::BufferTransferReleaseOnGraphics(cmd, frameCtx.m_prevTransforms_GPU);
			}
		}

		if (!lights.empty() && frameCtx.m_bLightsBufferUploadNeeded) {
			VkBufferCopy lightCpy{};
			lightCpy.srcOffset = 0;
			lightCpy.dstOffset = 0;
			lightCpy.size = totalLightBufferBytes;
			vkCmdCopyBuffer(cmd,
				lightStaging.m_buffer,
				frameCtx.m_lights_GPU.m_buffer,
				1,
				&lightCpy);

			BarrierUtils::BufferTransferReleaseOnGraphics(cmd, frameCtx.m_lights_GPU);
		}

	}, frameCtx.m_transferPool, QueueType::Transfer, Backend::GetDevice());

	frameCtx.CollectAndAppendCmds(std::move(DeferredCmdSubmitQueue::collectTransfer()), QueueType::Transfer);

	auto& transferSync = Renderer::_transferSync;
	const uint64_t signalValue = transferQueue.SubmitWithTimelineSync(
		frameCtx.transferCommands,
		transferSync.semaphore,
		++transferSync.signalValue
	);

	frameCtx.StashSubmitted(QueueType::Transfer);
	frameCtx.transferWaitValue = signalValue;

	// Assigns the cpu version to gpu version for verification of upload
	frameCtx.m_gpuAddressTable.SetGpuVersion(frameCtx.m_pendingAddressTableVersion);
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
	std::vector<VirtualInstance>& globalInstances,
	std::vector<glm::mat4>& globalTransforms,
	const double deltaTime)
{
	bool transformsUpdated = false;

	for (auto& inst : globalInstances) {
		SceneID sid = static_cast<SceneID>(inst.sceneID);
		SceneProfileEntry& profile = sceneProfiles.at(sid);

		if (profile.instanceCount == 1) {
			if (profile.drawType == InstancingMethod::DrawStatic) {
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
			if (profile.drawType == InstancingMethod::DrawDynamic) {
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
		//	if (profile.drawType == InstancingMethod::DrawDynamic) {
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
		if (profile.drawType == InstancingMethod::DrawMultiStatic || profile.instanceCount > 1) {
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
