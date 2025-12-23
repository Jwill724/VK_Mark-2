#include "pch.h"

#include "DrawPreparation.h"
#include "engine/Engine.h"
#include "utils/BufferUtils.h"

struct CombinedUploadPlan {
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

struct TransparentEntry {
	GPUInstance instance;
	AABB aabb; // Need index into worldaabbs for transparent depth sort
};

// All render data is reset prior to this each frame
void DrawPreparation::buildAndSortIndirectDraws(
	FrameContext& frameCtx,
	const std::vector<GPUMeshData>& meshes,
	const std::vector<AABB>& worldAABBs,
	const glm::vec4 cameraPos,
	const DebugToggles& meshStats)
{
	// Partition visible instances, while remembering their original indices
	std::vector<GPUInstance> opaqueInstances;
	std::vector<TransparentEntry> transparentEntries;

	opaqueInstances.reserve(frameCtx.visibleInstances.size());
	transparentEntries.reserve(frameCtx.visibleInstances.size());

	// === BATCH OPAQUE INSTANCES ===
	std::unordered_map<OpaqueBatchKey, std::vector<uint32_t>, OpaqueBatchKeyHash> opaqueBatches;

	// Separate pass types
	for (uint32_t i = 0; i < frameCtx.visibleInstances.size(); ++i) {
		const auto& inst = frameCtx.visibleInstances[i];
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

	// Opaque first
	frameCtx.opaqueRange.first = 0;
	for (const auto& [key, instanceIndices] : opaqueBatches) {
		const GPUMeshData& mesh = meshes[key.meshID];

		ASSERT(mesh.firstIndex + mesh.indexCount <= meshStats.indexCount &&
			"[DrawPrep] Opaque draws would read past end of index buffer.");
		ASSERT(mesh.vertexOffset + mesh.vertexCount <= meshStats.vertexCount &&
			"[DrawPrep] Opaque draws would read past end of vertex buffer.");

		VkDrawIndexedIndirectCommand cmd {
			.indexCount = mesh.indexCount,
			.instanceCount = static_cast<uint32_t>(instanceIndices.size()),
			.firstIndex = mesh.firstIndex,
			.vertexOffset = static_cast<int32_t>(mesh.vertexOffset),
			.firstInstance = frameCtx.opaqueRange.first + frameCtx.opaqueRange.visibleCount
		};

		frameCtx.indirectDraws.emplace_back(cmd);
		for (uint32_t idx : instanceIndices)
			frameCtx.visibleInstances.emplace_back(opaqueInstances[idx]);

		frameCtx.opaqueRange.visibleCount += cmd.instanceCount;
	}

	// === SORT AND BUILD TRANSPARENT ===
	if (!transparentEntries.empty()) {
		frameCtx.transparentRange.first = frameCtx.opaqueRange.visibleCount;
		frameCtx.transparentRange.visibleCount = static_cast<uint32_t>(transparentEntries.size());

		const glm::vec3 camPos = glm::vec3(cameraPos);
		std::sort(transparentEntries.begin(), transparentEntries.end(),
			[&](const TransparentEntry& a, const TransparentEntry& b) {
				return glm::length(a.aabb.origin - camPos) > glm::length(b.aabb.origin - camPos);
		});

		for (uint32_t i = 0; i < transparentEntries.size(); ++i) {
			const auto& entry = transparentEntries[i];
			const GPUMeshData& mesh = meshes[entry.instance.meshID];

			ASSERT(mesh.firstIndex + mesh.indexCount <= meshStats.indexCount &&
				"[DrawPrep] Transparent draws would read past end of index buffer.");
			ASSERT(mesh.vertexOffset + mesh.vertexCount <= meshStats.vertexCount &&
				"[DrawPrep] Transparent draws would read past end of vertex buffer.");

			VkDrawIndexedIndirectCommand cmd {
				.indexCount = mesh.indexCount,
				.instanceCount = 1,
				.firstIndex = mesh.firstIndex,
				.vertexOffset = static_cast<int32_t>(mesh.vertexOffset),
				.firstInstance = frameCtx.transparentRange.first + i
			};

			frameCtx.indirectDraws.push_back(cmd);
			frameCtx.visibleInstances.push_back(entry.instance);
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

static glm::mat4 makeGridTransform(uint32_t index, uint32_t count, float spacing) {
	uint32_t gridSize = static_cast<uint32_t>(std::ceil(std::sqrt(count)));
	uint32_t x = index % gridSize;
	uint32_t z = index / gridSize;

	glm::vec3 translation = glm::vec3(x * spacing, 0.0f, z * spacing);
	return glm::translate(glm::mat4(1.0f), translation);
}

void DrawPreparation::syncGlobalInstancesAndTransforms(
	FrameContext& frameCtx,
	GPUResources& gpuResources,
	std::unordered_map<SceneID, SceneProfileEntry>& sceneProfiles,
	std::vector<GlobalInstance>& globalInstances,
	std::vector<glm::mat4>& globalTransforms)
{
	bool anyTransformChanged = false;

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

				anyTransformChanged = true;
				continue;
			}
		}

		// TODO: ADD SUPPORT FOR MULTI-DYNAMIC

		// Defined from copy values append list or decrease list
		// on first run this will always be an append
		if (profile.drawType == DrawType::DrawMultiStatic || profile.instanceCount > 1) {
			// If instance count didn't change, skip
			if (inst.capacityCopies == profile.instanceCount) {
				continue;
			}

			inst.drawType = profile.drawType;

			glm::mat4 baseTransform = globalTransforms[inst.firstTransform];
			uint32_t currentCopies = inst.capacityCopies;
			uint32_t neededCopies = profile.instanceCount;

			fmt::print("[syncGI] multistatic: currentCopies={} neededCopies={} baseT={} staticTfSize(before)={}\n",
				currentCopies, neededCopies, inst.firstTransform, globalTransforms.size());

			if (neededCopies > currentCopies) {
				// Append new transforms
				for (uint32_t i = currentCopies; i < neededCopies; ++i) {
					glm::mat4 offset = makeGridTransform(i, neededCopies, 2.0f);
					globalTransforms.push_back(offset * baseTransform);
				}
				fmt::print("[syncGI] appended {} transforms, staticTfSize(after)={}\n",
					neededCopies - currentCopies, globalTransforms.size());
			}
			inst.capacityCopies = neededCopies;
			inst.transformCount = profile.instanceCount;

			frameCtx.transformsBufferUploadNeeded = true;
		}
	}

	if (anyTransformChanged) {
		frameCtx.transformsBufferUploadNeeded = true;
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