#include "pch.h"

#include "DrawPreparation.h"
#include "engine/Engine.h"
#include "utils/BufferUtils.h"

// All render data is reset prior to this each frame
void DrawPreparation::buildAndSortIndirectDraws(
	FrameContext& frameCtx,
	const std::vector<GPUMeshData>& meshes,
	const std::vector<AABB>& worldAABBs,
	const glm::vec4 cameraPos)
{
	// Partition visible instances, while remembering their original indices
	std::vector<GPUInstance> opaqueInstances;
	std::vector<GPUInstance> transparentInstances;
	std::vector<uint32_t> transparentVisIdx;

	opaqueInstances.reserve(frameCtx.visibleInstances.size());
	transparentInstances.reserve(frameCtx.visibleInstances.size());
	transparentVisIdx.reserve(frameCtx.visibleInstances.size());

	// === BATCH OPAQUE INSTANCES ===
	std::unordered_map<OpaqueBatchKey, std::vector<uint32_t>, OpaqueBatchKeyHash> opaqueBatches;

	// Separate pass types
	for (uint32_t i = 0; i < frameCtx.visibleInstances.size(); ++i) {
		const auto& inst = frameCtx.visibleInstances[i];
		if (static_cast<MaterialPass>(inst.passType) == MaterialPass::Opaque) {
			opaqueInstances.push_back(inst);
			const OpaqueBatchKey key{ inst.meshID, inst.materialID };
			opaqueBatches[key].push_back(i);
		}
		else {
			transparentInstances.push_back(inst);
			transparentVisIdx.push_back(i); // Need index into worldaabbs for transparent depth sort
		}
	}

	frameCtx.indirectDraws.reserve(opaqueBatches.size() + transparentInstances.size());

	// Rebuild instances in draw order
	frameCtx.visibleInstances.clear();
	frameCtx.visibleInstances.reserve(opaqueInstances.size() + transparentInstances.size());

	// Opaque first
	frameCtx.opaqueRange.first = 0;
	for (const auto& [key, instanceIndices] : opaqueBatches) {
		const GPUMeshData& mesh = meshes[key.meshID];

		ASSERT(mesh.firstIndex + mesh.indexCount <= frameCtx.drawDataPC.totalIndexCount &&
			"[DrawPrep] Opaque draws would read past end of index buffer.");
		ASSERT(mesh.vertexOffset + mesh.vertexCount <= frameCtx.drawDataPC.totalVertexCount &&
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
	if (!transparentInstances.empty()) {
		frameCtx.transparentRange.first = frameCtx.opaqueRange.visibleCount;
		frameCtx.transparentRange.visibleCount = static_cast<uint32_t>(transparentInstances.size());

		// Sort using AABBs aligned with the original visible list
		std::vector<uint32_t> order(transparentInstances.size());
		std::iota(order.begin(), order.end(), 0);

		const glm::vec3 camPos = glm::vec3(cameraPos);
		std::sort(order.begin(), order.end(), [&](uint32_t ia, uint32_t ib) {
			const auto& aabbA = worldAABBs[transparentVisIdx[ia]];
			const auto& aabbB = worldAABBs[transparentVisIdx[ib]];
			return glm::length(aabbA.origin - camPos) > glm::length(aabbB.origin - camPos);
		});

		for (uint32_t i = 0; i < order.size(); ++i) {
			const GPUInstance& inst = transparentInstances[order[i]];
			const GPUMeshData& mesh = meshes[inst.meshID];

			ASSERT(mesh.firstIndex + mesh.indexCount <= frameCtx.drawDataPC.totalIndexCount &&
				"[DrawPrep] Transparent draws would read past end of index buffer.");
			ASSERT(mesh.vertexOffset + mesh.vertexCount <= frameCtx.drawDataPC.totalVertexCount &&
				"[DrawPrep] Transparent draws would read past end of vertex buffer.");

			VkDrawIndexedIndirectCommand cmd {
				.indexCount = mesh.indexCount,
				.instanceCount = 1,
				.firstIndex = mesh.firstIndex,
				.vertexOffset = static_cast<int32_t>(mesh.vertexOffset),
				.firstInstance = frameCtx.transparentRange.first + i
			};

			frameCtx.indirectDraws.push_back(cmd);
			frameCtx.visibleInstances.push_back(inst);
		}
	}
}


void DrawPreparation::uploadGPUBuffersForFrame(FrameContext& frameCtx, GPUQueue& transferQueue, const VmaAllocator allocator) {
	ASSERT(frameCtx.combinedGPUStaging.buffer != VK_NULL_HANDLE &&
		"[DrawPreparation::uploadGPUBuffersForFrame] combinedGPUstaging buffer is invalid.");

	const size_t visInstBytes = frameCtx.visibleInstances.size() * sizeof(GPUInstance);
	const size_t indirectDrawBytes = frameCtx.indirectDraws.size() * sizeof(VkDrawIndexedIndirectCommand);
	const size_t addrBytes = sizeof(GPUAddressTable);

	uint8_t* const mappedStagingPtr = static_cast<uint8_t*>(frameCtx.combinedGPUStaging.info.pMappedData);
	const size_t stagingSize = frameCtx.combinedGPUStaging.info.size;

	const size_t visInstOffset = BufferUtils::reserveStaging(
		frameCtx.stagingHead,
		stagingSize,
		visInstBytes);
	const size_t indirectDrawOffset = BufferUtils::reserveStaging(
		frameCtx.stagingHead,
		stagingSize,
		indirectDrawBytes);
	const size_t addrOffset = BufferUtils::reserveStaging(
		frameCtx.stagingHead,
		stagingSize,
		addrBytes);

	// visible instances buffer staging
	memcpy(mappedStagingPtr + visInstOffset, frameCtx.visibleInstances.data(), visInstBytes);
	// indirect draws buffer staging
	memcpy(mappedStagingPtr + indirectDrawOffset, frameCtx.indirectDraws.data(), indirectDrawBytes);
	// frame address table staging
	memcpy(mappedStagingPtr + addrOffset, &frameCtx.addressTable, addrBytes);

	const auto bufAlloc = frameCtx.combinedGPUStaging.allocation;
	BufferUtils::flushStagingRange(bufAlloc, visInstOffset, visInstBytes, allocator);
	BufferUtils::flushStagingRange(bufAlloc, indirectDrawOffset, indirectDrawBytes, allocator);
	BufferUtils::flushStagingRange(bufAlloc, addrOffset, addrBytes, allocator);

	// Record big transfer copies for indirect, instance, and main frame address table buffers
	CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {

		// visible instance data
		VkBufferCopy visInstCpy{};
		visInstCpy.srcOffset = visInstOffset;
		visInstCpy.dstOffset = 0;
		visInstCpy.size = visInstBytes;
		vkCmdCopyBuffer(cmd,
			frameCtx.combinedGPUStaging.buffer,
			frameCtx.visibleInstancesBuffer.buffer,
			1,
			&visInstCpy);

		// indirect draw commands
		VkBufferCopy indirectDrawsCpy{};
		indirectDrawsCpy.srcOffset = indirectDrawOffset;
		indirectDrawsCpy.dstOffset = 0;
		indirectDrawsCpy.size = indirectDrawBytes;
		vkCmdCopyBuffer(cmd,
			frameCtx.combinedGPUStaging.buffer,
			frameCtx.indirectDrawsBuffer.buffer,
			1,
			&indirectDrawsCpy);

		// GPU address table copy
		VkBufferCopy addressCpy{};
		addressCpy.srcOffset = addrOffset;
		addressCpy.dstOffset = 0;
		addressCpy.size = addrBytes;
		vkCmdCopyBuffer(cmd,
			frameCtx.combinedGPUStaging.buffer,
			frameCtx.addressTableBuffer.buffer,
			1,
			&addressCpy);

		frameCtx.addressTableDirty = true;

		BarrierUtils::releaseTransferToShaderReadQ(cmd, frameCtx.addressTableBuffer);

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

static glm::mat4 backAndForthX(float step = 0.03f, float minX = -2.0f, float maxX = 2.0f) {
	static float x = 0.0f;
	static float dir = 1.0f;
	x += dir * step;
	if (x >= maxX) { x = maxX; dir = -1.0f; }
	if (x <= minX) { x = minX; dir = 1.0f; }
	return glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, 0.0f));
}

void DrawPreparation::syncGlobalInstancesAndTransforms(
	FrameContext& frameCtx,
	GPUResources& gpuResources,
	std::unordered_map<SceneID, SceneProfileEntry>& sceneProfiles,
	std::vector<GlobalInstance>& globalInstances,
	std::vector<glm::mat4>& globalTransforms,
	GPUQueue& transferQueue)
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
				M = glm::rotate(glm::mat4(1.0f), 0.005f, glm::vec3(0.0f, 1.0f, 0.0f)) * M;
				//M = backAndForthX(0.03f, -1.5f, 1.5f) * M;

				anyTransformChanged = true;
				continue;
			}
		}

		// Defined from copy values append list or decrease list
		// on first run this will always be an append
		//if (profile.drawType == DrawType::DrawMultiStatic || profile.instanceCount > 1) {
		//	// If instance count didn't change, skip
		//	if (inst.capacityCopies + 1 == profile.instanceCount) {
		//		continue;
		//	}

		//	inst.drawType = DrawType::DrawMultiStatic;

		//	glm::mat4 baseTransform = globalTransforms[inst.firstTransform];
		//	uint32_t currentCopies = inst.capacityCopies;
		//	uint32_t neededCopies = profile.instanceCount;

		//	fmt::print("[syncGI] multistatic: currentCopies={} neededCopies={} baseT={} staticTfSize(before)={}\n",
		//		currentCopies, neededCopies, inst.firstTransform, globalTransforms.size());

		//	if (neededCopies > currentCopies) {
		//		// Append new transforms
		//		for (uint32_t i = currentCopies; i < neededCopies; ++i) {
		//			glm::mat4 offset = makeGridTransform(i, neededCopies, 2.0f);
		//			globalTransforms.push_back(offset * baseTransform);
		//		}
		//		fmt::print("[syncGI] appended {} transforms, staticTfSize(after)={}\n",
		//			neededCopies - currentCopies, globalTransforms.size());
		//	}
		//	inst.capacityCopies = neededCopies;
		//	inst.transformCount = profile.instanceCount;

		//	frameCtx.transformsBufferUploadNeeded = true;
		//}
	}

	auto& globalAddrsTableBuf = gpuResources.getAddressTableBuffer();
	auto& globalAddrsTable = gpuResources.getAddressTable();
	const auto allocator = gpuResources.getAllocator();

	const size_t transformsBytes = globalTransforms.size() * sizeof(glm::mat4);
	const size_t addrBytes = sizeof(GPUAddressTable);

	// First time creation on frame 0
	if (!gpuResources.containsGPUBuffer(AddressBufferType::Transforms)) {
		fmt::print("[syncGobalInstances] create Transforms GPU buffer\n");
		AllocatedBuffer newTransformBuf = BufferUtils::createGPUAddressBuffer(
			AddressBufferType::Transforms,
			globalAddrsTable,
			transformsBytes,
			allocator);
		// Note: The current global address table gets marked dirty, only the internal upload function marks it back to false.
		gpuResources.addGPUBufferToGlobalAddress(AddressBufferType::Transforms, newTransformBuf);
		fmt::print("[syncGobalInstances] new buffer=0x{:x} size={}\n", (uint64_t)newTransformBuf.buffer, newTransformBuf.info.size);

		frameCtx.transformsBufferUploadNeeded = true;
	}

	if (anyTransformChanged) {
		frameCtx.transformsBufferUploadNeeded = true;
	}

	if (!frameCtx.transformsBufferUploadNeeded) return;

	auto& transformsBuf = gpuResources.getGPUAddrsBuffer(AddressBufferType::Transforms);

	uint8_t* const mappedStagingPtr = static_cast<uint8_t*>(frameCtx.combinedGPUStaging.info.pMappedData);
	const size_t stagingSize = frameCtx.combinedGPUStaging.info.size;

	const size_t transformOffset = BufferUtils::reserveStaging(
		frameCtx.stagingHead,
		stagingSize,
		transformsBytes);
	const size_t addrOffset = BufferUtils::reserveStaging(
		frameCtx.stagingHead,
		stagingSize,
		addrBytes);

	memcpy(mappedStagingPtr + transformOffset, globalTransforms.data(), transformsBytes);
	memcpy(mappedStagingPtr + addrOffset, &globalAddrsTable, addrBytes);

	const auto bufAlloc = frameCtx.combinedGPUStaging.allocation;
	BufferUtils::flushStagingRange(bufAlloc, transformOffset, transformsBytes, allocator);
	BufferUtils::flushStagingRange(bufAlloc, addrOffset, addrBytes, allocator);

	const auto device = Backend::getDevice();

	// Upload transforms and update global address table
	CommandBuffer::recordDeferredCmd([&](VkCommandBuffer cmd) {
		// indirect draw commands
		VkBufferCopy transformsCpy{};
		transformsCpy.srcOffset = transformOffset;
		transformsCpy.dstOffset = 0;
		transformsCpy.size = transformsBytes;
		vkCmdCopyBuffer(cmd,
			frameCtx.combinedGPUStaging.buffer,
			transformsBuf.buffer,
			1,
			&transformsCpy);

		VkBufferCopy addressTableCpy{};
		addressTableCpy.srcOffset = addrOffset;
		addressTableCpy.dstOffset = 0;
		addressTableCpy.size = addrBytes;
		vkCmdCopyBuffer(cmd,
			frameCtx.combinedGPUStaging.buffer,
			globalAddrsTableBuf.buffer,
			1,
			&addressTableCpy);

		BarrierUtils::releaseTransferToShaderReadQ(cmd, globalAddrsTableBuf);

	}, frameCtx.transferPool, QueueType::Transfer, device);

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