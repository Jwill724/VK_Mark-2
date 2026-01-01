#include "pch.h"

#include "DrawPreparation.h"
#include "engine/Engine.h"
#include "utils/BufferUtils.h"

static struct CombinedUploadPlan {
	// frame-local
	size_t visOff = 0, visSize = 0; // visible instances
	size_t indOff = 0, indSize = 0; // indirect draws

	// transforms
	size_t curTransformOff = 0, curTransformSize = 0;
	size_t prevTransformOff = 0, prevTransformSize = 0;

	// Only required when visibles and draw buffers are created or destroyed.
	// per-frame address table (frameCtx.addressTableBuffer)
	size_t fAddrOff = 0, fAddrSize = 0;
};

static struct TransparentEntry {
	GPUInstance instance;
	AABB aabb; // Need index into worldaabbs for transparent depth sort
};


// All render data is reset prior to this each frame
void DrawPreparation::buildAndSortIndirectDraws(
	FrameContext& frameCtx,
	const std::vector<GPUMeshData>& meshes,
	const std::vector<MeshLODs>& meshLods,
	const std::vector<AABB>& worldAABBs,
	const glm::vec4& cameraPos,
	const glm::mat4& cameraProj,
	const DebugToggles& meshStats)
{
	std::vector<GPUInstance> opaqueInstances;
	std::vector<TransparentEntry> transparentEntries;

	opaqueInstances.reserve(frameCtx.visibleInstances.size());
	transparentEntries.reserve(frameCtx.visibleInstances.size());

	const glm::vec3 camPos = glm::vec3(cameraPos);

	// === BATCH OPAQUE INSTANCES ===
	std::unordered_map<OpaqueBatchKey, std::vector<uint32_t>, OpaqueBatchKeyHash> opaqueBatches;

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

		if (static_cast<MaterialPass>(inst.passType) == MaterialPass::Opaque) {
			uint32_t opaqueIndex = static_cast<uint32_t>(opaqueInstances.size());
			opaqueInstances.push_back(inst);
			const OpaqueBatchKey key{ inst.meshID, inst.materialID };
			opaqueBatches[key].push_back(opaqueIndex);
		}
		else {
			transparentEntries.push_back({ inst, worldAABBs[i] });
		}
	}

	frameCtx.indirectDraws.reserve(opaqueBatches.size() + transparentEntries.size());

	// Rebuild instances in draw order
	frameCtx.visibleInstances.clear();
	frameCtx.visibleInstances.reserve(opaqueInstances.size() + transparentEntries.size());

	// === OPAQUE BATCHES ===
	frameCtx.opaqueRange.first = 0;
	for (const auto& [batchKey, instanceIndices] : opaqueBatches) {
		const GPUMeshData& mesh = meshes[batchKey.meshID];

		ASSERT(mesh.firstIndex + mesh.indexCount <= meshStats.indexCount &&
			"[DrawPrep] Opaque draws would read past end of index buffer.");
		ASSERT(mesh.vertexOffset + mesh.vertexCount <= meshStats.vertexCount &&
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
			frameCtx.visibleInstances.push_back(opaqueInstances[idx]);
		}

		frameCtx.opaqueRange.commandCount++;
		frameCtx.opaqueRange.visibleCount += cmd.instanceCount;
	}

	// === SORT AND BUILD TRANSPARENT ===
	if (!transparentEntries.empty()) {
		frameCtx.transparentRange.first = frameCtx.opaqueRange.commandCount;
		frameCtx.transparentRange.visibleCount = static_cast<uint32_t>(transparentEntries.size());

		std::sort(transparentEntries.begin(), transparentEntries.end(),
			[&](const TransparentEntry& a, const TransparentEntry& b) {
				return glm::length(a.aabb.origin - camPos) > glm::length(b.aabb.origin - camPos);
			});

		for (uint32_t i = 0; i < transparentEntries.size(); ++i) {
			const TransparentEntry& entry = transparentEntries[i];
			const GPUMeshData& mesh = meshes[entry.instance.meshID];

			ASSERT(mesh.firstIndex + mesh.indexCount <= meshStats.indexCount &&
				"[DrawPrep] Transparent draws would read past end of index buffer.");
			ASSERT(mesh.vertexOffset + mesh.vertexCount <= meshStats.vertexCount &&
				"[DrawPrep] Transparent draws would read past end of vertex buffer.");

			const uint32_t firstInstance = static_cast<uint32_t>(frameCtx.visibleInstances.size());

			VkDrawIndexedIndirectCommand cmd{};
			cmd.indexCount = mesh.indexCount;
			cmd.instanceCount = 1;
			cmd.firstIndex = mesh.firstIndex;
			cmd.vertexOffset = static_cast<int32_t>(mesh.vertexOffset);
			cmd.firstInstance = firstInstance;

			frameCtx.indirectDraws.push_back(cmd);
			frameCtx.visibleInstances.push_back(entry.instance);

			frameCtx.transparentRange.commandCount++;
		}
	}
}


inline static CombinedUploadPlan stageCombinedUploads(FrameContext& frame,
	const std::vector<glm::mat4>& curTransforms,
	const std::vector<glm::mat4>& prevTransforms,
	VmaAllocator alloc)
{
	CombinedUploadPlan plan{};
	auto* mapped = static_cast<uint8_t*>(frame.combinedGPUStaging.info.pMappedData);
	const size_t cap = frame.combinedGPUStaging.info.size;

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

	// transforms
	if (frame.transformsBufferUploadNeeded) {
		plan.curTransformSize = curTransforms.size() * sizeof(glm::mat4);
		plan.curTransformOff = BufferUtils::reserveStaging(frame.stagingHead, cap, plan.curTransformSize);
		memcpy(mapped + plan.curTransformOff, curTransforms.data(), plan.curTransformSize);
		BufferUtils::flushStagingRange(frame.combinedGPUStaging.allocation, plan.curTransformOff, plan.curTransformSize, alloc);

		plan.prevTransformSize = prevTransforms.size() * sizeof(glm::mat4);
		plan.prevTransformOff = BufferUtils::reserveStaging(frame.stagingHead, cap, plan.prevTransformSize);
		memcpy(mapped + plan.prevTransformOff, prevTransforms.data(), plan.prevTransformSize);
		BufferUtils::flushStagingRange(frame.combinedGPUStaging.allocation, plan.prevTransformOff, plan.prevTransformSize, alloc);
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
	const std::vector<glm::mat4>& prevTransforms,
	GPUQueue& transferQueue)
{
	ASSERT(frameCtx.combinedGPUStaging.buffer != VK_NULL_HANDLE &&
		"[DrawPreparation::uploadGPUBuffersForFrame] combinedGPUstaging buffer is invalid.");

	CombinedUploadPlan plan = stageCombinedUploads(frameCtx, transforms, prevTransforms, gpuResources.getAllocator());

	// Record big transfer copies for indirect, instance, frame address table buffer, and global transforms/global address table
	CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {
		// visible instance data
		VkBufferCopy visInstCpy{};
		visInstCpy.srcOffset = plan.visOff;
		visInstCpy.dstOffset = 0;
		visInstCpy.size = plan.visSize;
		vkCmdCopyBuffer(cmd,
			frameCtx.combinedGPUStaging.buffer,
			frameCtx.visibleInstancesBuffer.buffer,
			1,
			&visInstCpy);

		// indirect draw commands
		VkBufferCopy indirectDrawsCpy{};
		indirectDrawsCpy.srcOffset = plan.indOff;
		indirectDrawsCpy.dstOffset = 0;
		indirectDrawsCpy.size = plan.indSize;
		vkCmdCopyBuffer(cmd,
			frameCtx.combinedGPUStaging.buffer,
			frameCtx.indirectDrawsBuffer.buffer,
			1,
			&indirectDrawsCpy);

		BarrierUtils::releaseTransferToShaderReadQ(cmd, frameCtx.visibleInstancesBuffer);
		BarrierUtils::releaseTransferToIndirectQ(cmd, frameCtx.indirectDrawsBuffer);

		if (frameCtx.transformsBufferUploadNeeded) {
			const auto& transformsBuf = gpuResources.getGPUAddrsBuffer(AddressBufferType::Transforms);
			const auto& prevTransformsBuf = gpuResources.getGPUAddrsBuffer(AddressBufferType::PrevTransforms);

			// Current transforms
			VkBufferCopy curTransformsCpy{};
			curTransformsCpy.srcOffset = plan.curTransformOff;
			curTransformsCpy.dstOffset = 0;
			curTransformsCpy.size = plan.curTransformSize;
			vkCmdCopyBuffer(cmd,
				frameCtx.combinedGPUStaging.buffer,
				transformsBuf.buffer,
				1,
				&curTransformsCpy);

			// Previous transforms
			VkBufferCopy prevTransformsCpy{};
			prevTransformsCpy.srcOffset = plan.prevTransformOff;
			prevTransformsCpy.dstOffset = 0;
			prevTransformsCpy.size = plan.prevTransformSize;
			vkCmdCopyBuffer(cmd,
				frameCtx.combinedGPUStaging.buffer,
				prevTransformsBuf.buffer,
				1,
				&prevTransformsCpy);

			BarrierUtils::releaseTransferToShaderReadQ(cmd, transformsBuf);
			BarrierUtils::releaseTransferToShaderReadQ(cmd, prevTransformsBuf);
		}

		if (frameCtx.addressTable.isTableDirty()) {
			// frame GPU address table copy
			VkBufferCopy frameAddressTableCpy{};
			frameAddressTableCpy.srcOffset = plan.fAddrOff;
			frameAddressTableCpy.dstOffset = 0;
			frameAddressTableCpy.size = plan.fAddrSize;
			vkCmdCopyBuffer(cmd,
				frameCtx.combinedGPUStaging.buffer,
				frameCtx.addressTableBuffer.buffer,
				1,
				&frameAddressTableCpy);

			frameCtx.addressTable.clearTableDirty();
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


void DrawPreparation::syncGlobalInstancesAndTransforms(
	FrameContext& frameCtx,
	GPUResources& gpuResources,
	std::unordered_map<SceneID, SceneProfileEntry>& sceneProfiles,
	std::vector<GlobalInstance>& globalInstances,
	std::vector<glm::mat4>& globalTransforms)
{
	for (auto& inst : globalInstances) {
		SceneID sid = static_cast<SceneID>(inst.sceneID);
		SceneProfileEntry& profile = sceneProfiles.at(sid);

		if (profile.instanceCount == 1) {
			if (profile.drawType == DrawType::DrawStatic) {
				inst.drawType = profile.drawType;
				continue; // transforms already baked into static
			}
			if (profile.drawType == DrawType::DrawDynamic) {
				inst.drawType = profile.drawType;
				glm::mat4& M = globalTransforms[inst.firstTransform];

				// Spin in place around local pivot
				glm::vec3 pivot = glm::vec3(M[3]); // Extract world space position
				glm::mat4 T = glm::translate(glm::mat4(1.0f), pivot);
				glm::mat4 R = glm::rotate(glm::mat4(1.0f), 0.01f, glm::vec3(0.0f, 1.0f, 0.0f));
				glm::mat4 T_inv = glm::translate(glm::mat4(1.0f), -pivot);

				M = T * R * T_inv * M;

				frameCtx.transformsBufferUploadNeeded = true;
				continue;
			}
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
				frameCtx.transformsBufferUploadNeeded = true;
			}

			// Change active copies (visibility will rebuild/activate).
			inst.usedCopies = desiredUsedCopies;
		}
	}

	// First time creation on frame 0
	if (!gpuResources.containsGPUBuffer(AddressBufferType::Transforms) &&
		!gpuResources.containsGPUBuffer(AddressBufferType::PrevTransforms)) {
		auto& globalAddrsTable = gpuResources.getAddressTable();
		const auto allocator = gpuResources.getAllocator();

		const size_t transformsBytes = globalTransforms.size() * sizeof(glm::mat4);

		AllocatedBuffer newTransformBuf = BufferUtils::createGPUAddressBuffer(
			AddressBufferType::Transforms,
			globalAddrsTable,
			transformsBytes,
			allocator);

		AllocatedBuffer newPrevTransformBuf = BufferUtils::createGPUAddressBuffer(
			AddressBufferType::PrevTransforms,
			globalAddrsTable,
			transformsBytes,
			allocator);

		// Note: The current global address table gets marked dirty, only the internal upload function marks it back to false.
		gpuResources.addGPUBufferToGlobalAddress(AddressBufferType::Transforms, newTransformBuf);
		gpuResources.addGPUBufferToGlobalAddress(AddressBufferType::PrevTransforms, newPrevTransformBuf);

		frameCtx.transformsBufferUploadNeeded = true;
	}
}